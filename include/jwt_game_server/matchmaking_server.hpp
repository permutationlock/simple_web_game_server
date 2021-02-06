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
        m_altered_sessions.clear();
      }
      m_match_cond.notify_all();
    }

    std::size_t get_player_count() {
      return m_jwt_server.get_player_count();
    }

    bool is_running() {
      return m_jwt_server.is_running();
    }

    void match_players(std::chrono::milliseconds timestep) {
      auto time_start = clock::now();
      while(m_jwt_server.is_running()) {
        unique_lock<mutex> lock(m_match_lock);

        while(!m_matchmaker.can_match(
            m_session_data, m_altered_sessions
          ))
        {
          m_match_cond.wait(lock);
          if(!m_jwt_server.is_running()) {
            return;
          }
        }

        auto delta_time =
          std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now() - time_start
          );

        if(delta_time >= timestep) {
          vector<typename matchmaker::game> games;
          m_matchmaker.match(
              games, m_session_data, m_altered_sessions
            );
          m_altered_sessions.clear();

          for(const auto& g : games) {
            for(const session_id& sid : g.session_list) {
              m_session_data.erase(sid);
            }
          }

          lock.unlock();

          for(const auto& g : games) {
            spdlog::trace("matched game: {}", g.data.dump());
            for(const session_id& sid : g.session_list) {
              m_jwt_server.complete_session(sid, g.session, g.data);
            }
          }

          time_start = clock::now();
        } else {
          lock.unlock();
        }

        std::this_thread::sleep_for(std::min(1ms, timestep-delta_time));
      }
    }

  private:
    // proper procedure for client to cancel matchmaking is to send a message
    // and wait for the server to close the connection; by acquiring the match
    // lock and marking the user as unavailable we avoid the situation where a
    // player disconnects after the match function is called, but before a game
    // token is successfully sent to them
    void process_message(const combined_id& id, const json& data) {
      unique_lock<mutex> lock(m_match_lock);
      auto session_data_it = m_session_data.find(id.session);
      if(session_data_it != m_session_data.end()) {
        json cancel_msg = m_matchmaker.get_cancel_data(
            id.session, session_data_it->second
          );

        m_altered_sessions.insert(id.session);
        m_session_data.erase(session_data_it);

        lock.unlock();

        m_jwt_server.complete_session(
            id.session,
            id.session,
            cancel_msg
          );
      }
    }

    void player_connect(const combined_id& id, const json& data) {
      session_data d{data};
      if(!d.is_valid()) {
        spdlog::debug("error with player json: {}", data.dump());
        m_jwt_server.complete_session(
            id.session,
            id.session,
            m_matchmaker.get_invalid_data()
          );
        return;
      }

      {
        lock_guard<mutex> guard(m_match_lock);
        if(m_session_data.count(id.session) == 0) {
          m_session_data.emplace(std::make_pair(id.session, d));
          m_altered_sessions.insert(id.session);
        }
      }

      m_match_cond.notify_one();
    }

    void player_disconnect(const combined_id& id) {
      unique_lock<mutex> lock(m_match_lock);
      auto session_data_it = m_session_data.find(id.session);
      if(session_data_it != m_session_data.end()) {
        json cancel_msg = m_matchmaker.get_cancel_data(
            id.session, session_data_it->second
          );

        m_altered_sessions.insert(id.session);
        m_session_data.erase(session_data_it);

        lock.unlock();

        m_jwt_server.complete_session(
            id.session,
            id.session,
            cancel_msg
          );
      }
    }

    // member variables
    matchmaker m_matchmaker;

    // m_match_lock guards the members m_session_map and m_altered_sessions
    mutex m_match_lock;
    condition_variable m_match_cond;

    unordered_map<session_id, session_data, id_hash> m_session_data;
    unordered_set<session_id, id_hash> m_altered_sessions;

    jwt_base_server m_jwt_server;
  };
}

#endif // JWT_GAME_SERVER_MATCHMAKING_SERVER_HPP
