#ifndef JWT_GAME_SERVER_MATCHMAKING_SERVER_HPP
#define JWT_GAME_SERVER_MATCHMAKING_SERVER_HPP

#include "base_server.hpp"

#include <unordered_set>
#include <chrono>
#include <functional>

namespace jwt_game_server {
  // Time literals to initialize timestep variables
  using namespace std::chrono_literals;

  // datatype implementations
  using std::unordered_set;

  template<typename matchmaker, typename jwt_clock, typename json_traits,
    typename server_config, typename close_reasons = default_close_reasons>
  class matchmaking_server {
  // type definitions
  private:
    using jwt_base_server = base_server<
        typename matchmaker::player_traits,
        jwt_clock,
        json_traits,
        server_config,
        close_reasons
      >;

    using combined_id = typename jwt_base_server::combined_id;
    using player_id = typename jwt_base_server::player_id;
    using session_id = typename jwt_base_server::session_id;
    using id_hash = typename jwt_base_server::id_hash;
 
    using json = typename jwt_base_server::json;
    using session_data = typename matchmaker::session_data;
    using clock = typename jwt_base_server::clock;

    struct connection_update {
      connection_update(const session_id& i) : session(i),
        disconnection(true) {}
      connection_update(const session_id& i, const json& d) : session(i),
        data(d), disconnection(false) {}

      session_id session;
      json data;
      bool disconnection;
    };

  // main class body
  public:
    matchmaking_server(
        const jwt::verifier<jwt_clock, json_traits>& v,
        function<std::string(const combined_id&, const json&)> f,
        std::chrono::milliseconds t
      ) : m_jwt_server{v, f, t}
    {
      m_jwt_server.set_open_handler(
          bind(
            &matchmaking_server::player_connect,
            this,
            jwt_game_server::_1,
            jwt_game_server::_2
          )
        );
      m_jwt_server.set_close_handler(
          bind(
            &matchmaking_server::player_disconnect,
            this,
            jwt_game_server::_1
          )
        );
      m_jwt_server.set_message_handler(
          bind(
            &matchmaking_server::process_message,
            this,
            jwt_game_server::_1,
            jwt_game_server::_2
          )
        );
    }

    matchmaking_server(
        const jwt::verifier<jwt_clock, json_traits>& v,
        function<std::string(const combined_id&, const json&)> f
      ) : matchmaking_server{v, f, 3600s} {}

    void run(uint16_t port, bool unlock_address = false) {
      m_jwt_server.run(port, unlock_address);
    }

    void process_messages() {
      m_jwt_server.process_messages();
    }

    void reset() {
      stop();
      m_jwt_server.reset();
    }

    void stop() {
      m_jwt_server.stop();
      {
        lock_guard<mutex> guard(m_match_lock);
        m_session_data.clear();
        m_connection_updates.clear();
      }
    }

    std::size_t get_player_count() {
      return m_jwt_server.get_player_count();
    }

    bool is_running() {
      return m_jwt_server.is_running();
    }

    void match_players(std::chrono::milliseconds timestep) {
      auto time_start = clock::now();
      vector<session_id> finished_sessions;

      while(m_jwt_server.is_running()) {
        unique_lock<mutex> match_lock(m_match_lock);

        if(!m_matchmaker.can_match(m_session_data)) {
          match_lock.unlock();
          unique_lock<mutex> conn_lock(m_connection_update_list_lock);
          while(m_connection_updates.empty()) {
            m_match_condition.wait(conn_lock);
            if(!m_jwt_server.is_running()) {
              return;
            }
          }
          match_lock.lock();
        }

        auto delta_time =
          std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now() - time_start
          );
        const long dt_count = delta_time.count();

        if(delta_time < timestep) {
          match_lock.unlock();
          std::this_thread::sleep_for(std::min(1us, timestep-delta_time+1us));
        } else {
          time_start = clock::now();
          {
            vector<session_id> new_finished_sessions
              = process_connection_updates();

            // we remove data here to catch any possible players submitting
            // connections in the last timestep when the session ends
            for(const session_id& sid : finished_sessions) {
              spdlog::trace("erasing data for session {}", sid);
              m_session_data.erase(sid);
            }

            std::swap(finished_sessions, new_finished_sessions);
          }

          vector<typename matchmaker::game> games;
          m_matchmaker.match(games, m_session_data, dt_count);
 
          for(const auto& g : games) {
            spdlog::trace("matched game: {}", g.data.dump());
            for(const session_id& sid : g.session_list) {
              m_jwt_server.complete_session(sid, g.session, g.data);
              finished_sessions.push_back(sid);
            }
          }
        }
      }
    }

  private:
    vector<session_id> process_connection_updates() {
      vector<connection_update> connection_updates;
      {
        unique_lock<mutex> conn_lock(m_connection_update_list_lock);
        std::swap(connection_updates, m_connection_updates);
      }

      vector<session_id> finished_sessions;
      for(connection_update& update : connection_updates) {
        auto it = m_session_data.find(update.session);
        if(update.disconnection) {
          spdlog::trace(
              "processiong disconnection for session {}", update.session
            );
          if(it != m_session_data.end()) {
            m_jwt_server.complete_session(
                update.session,
                update.session,
                m_matchmaker.get_cancel_data()
              );
            m_session_data.erase(update.session);
            finished_sessions.push_back(update.session);
          }
        } else {
          spdlog::trace(
              "processiong connection for session {}", update.session
            );
          if(it == m_session_data.end()) {
            session_data data{update.data};

            if(data.is_valid()) {
              m_session_data.emplace(
                  update.session, std::move(data)
                );
            } else {
              m_jwt_server.complete_session(
                  update.session,
                  update.session,
                  m_matchmaker.get_cancel_data()
                );
              finished_sessions.push_back(update.session);
            }
          }
        }
      }

      return finished_sessions;
    }

    // proper procedure for client to cancel matchmaking is to send a message
    // and wait for the server to close the connection; by acquiring the match
    // lock and marking the user as unavailable we avoid the situation where a
    // player disconnects after the match function is called, but before a game
    // token is successfully sent to them
    void process_message(const combined_id& id, const std::string& data) {
      {
        lock_guard<mutex> guard(m_connection_update_list_lock);
        m_connection_updates.emplace_back(id.session);
      }
      m_match_condition.notify_one();
    }

    void player_connect(const combined_id& id, const json& data) {
      {
        lock_guard<mutex> guard(m_connection_update_list_lock);
        m_connection_updates.emplace_back(id.session, data);
      }
      m_match_condition.notify_one();
    }

    void player_disconnect(const combined_id& id) {
      {
        lock_guard<mutex> guard(m_connection_update_list_lock);
        m_connection_updates.emplace_back(id.session);
      }
      m_match_condition.notify_one();
    }

    // member variables
    matchmaker m_matchmaker;

    unordered_map<session_id, session_data, id_hash> m_session_data;
    mutex m_match_lock;

    std::vector<connection_update> m_connection_updates;
    mutex m_connection_update_list_lock;

    condition_variable m_match_condition;

    jwt_base_server m_jwt_server;
  };
}

#endif // JWT_GAME_SERVER_MATCHMAKING_SERVER_HPP
