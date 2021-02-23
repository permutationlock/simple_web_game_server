#ifndef JWT_GAME_SERVER_MATCHMAKING_SERVER_HPP
#define JWT_GAME_SERVER_MATCHMAKING_SERVER_HPP

#include "base_server.hpp"

#include <unordered_set>
#include <chrono>
#include <functional>
#include <tuple>

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
    using clock = typename jwt_base_server::clock;

    using game = std::tuple<vector<session_id>, session_id, json>;
    using message = pair<session_id, std::string>;
    using session_data = typename matchmaker::session_data;

    struct connection_update {
      connection_update(const combined_id& i) : id(i),
        disconnection(true) {}
      connection_update(const combined_id& i, json&& d) : id(i),
        data(std::move(d)), disconnection(false) {}

      combined_id id;
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

    void set_tls_init_handler(function<ssl_context_ptr(connection_hdl)> f) {
      m_jwt_server.set_tls_init_handler(f);
    }

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
        m_session_players.clear();
        m_connection_updates.clear();
      }
      m_match_condition.notify_one();
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
              m_session_players.erase(sid);
            }

            std::swap(finished_sessions, new_finished_sessions);
          }

          vector<game> games;
          {
            vector<pair<session_id, std::string> > messages;
            m_matchmaker.match(games, messages, m_session_data, dt_count);

            for(message& msg : messages) {
              auto session_players_it = m_session_players.find(msg.first);
              if(session_players_it != m_session_players.end()) {
                for(player_id pid : session_players_it->second) {
                  m_jwt_server.send_message(
                      { pid, msg.first }, std::move(msg.second)
                    );
                }
              }
            }
          }
 
          for(game& g : games) {
            vector<session_id> sessions;
            session_id game_sid;
            json game_data;
            std::tie(sessions, game_sid, game_data) = std::move(g);

            spdlog::trace("matched game: {}", game_data.dump());

            for(session_id& sid : sessions) {
              m_jwt_server.complete_session(
                  sid, game_sid, std::move(game_data)
                );
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
        auto it = m_session_data.find(update.id.session);
        if(update.disconnection) {
          if(it != m_session_data.end()) {
            spdlog::trace(
                "processiong disconnection for session {}", update.id.session
              );
            m_jwt_server.complete_session(
                update.id.session,
                update.id.session,
                m_matchmaker.get_cancel_data()
              );
            m_session_data.erase(it);
            m_session_players.erase(update.id.session);
            finished_sessions.push_back(update.id.session);
          }
        } else {
          spdlog::trace(
              "processiong connection for session {}", update.id.session
            );
          if(it == m_session_data.end()) {
            session_data data{update.data};

            if(data.is_valid()) {
              m_session_data.emplace(
                  update.id.session, std::move(data)
                );
              m_session_players.emplace(
                  update.id.session, set<player_id>{ update.id.player }
                );
            } else {
              m_jwt_server.complete_session(
                  update.id.session,
                  update.id.session,
                  m_matchmaker.get_cancel_data()
                );
              finished_sessions.push_back(update.id.session);
            }
          } else {
            m_session_players.at(update.id.session).insert(update.id.player);
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
    void process_message(const combined_id& id, std::string&& data) {
      {
        lock_guard<mutex> guard(m_connection_update_list_lock);
        m_connection_updates.emplace_back(id);
      }
      m_match_condition.notify_one();
    }

    void player_connect(const combined_id& id, json&& data) {
      {
        lock_guard<mutex> guard(m_connection_update_list_lock);
        m_connection_updates.emplace_back(
            id, std::move(data)
          );
      }
      m_match_condition.notify_one();
    }

    void player_disconnect(const combined_id& id) {
      {
        lock_guard<mutex> guard(m_connection_update_list_lock);
        m_connection_updates.emplace_back(id);
      }
      m_match_condition.notify_one();
    }

    // member variables
    matchmaker m_matchmaker;

    unordered_map<session_id, session_data, id_hash> m_session_data;
    unordered_map<session_id, set<player_id>, id_hash> m_session_players;
    mutex m_match_lock;

    std::vector<connection_update> m_connection_updates;
    mutex m_connection_update_list_lock;

    condition_variable m_match_condition;

    jwt_base_server m_jwt_server;
  };
}

#endif // JWT_GAME_SERVER_MATCHMAKING_SERVER_HPP
