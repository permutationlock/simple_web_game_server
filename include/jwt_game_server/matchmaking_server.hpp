#ifndef JWT_GAME_SERVER_GAME_SERVER_HPP
#define JWT_GAME_SERVER_GAME_SERVER_HPP

#include "base_server.hpp"

#include <set>
#include <chrono>
#include <functional>

namespace jwt_game_server {
  // Time literals to initialize timestep variables
  using namespace std::chrono_literals;

  // datatype implementations
  using std::set;

  template<typename matchmaker, typename jwt_clock, typename json_traits,
    typename server_config>
  class matchmaking_server : public base_server<
        typename matchmaker::player_traits,
        jwt_clock,
        json_traits,
        server_config
      >
  {
  // type definitions
  private:
    using super = base_server<
        typename matchmaker::player_traits,
        jwt_clock,
        json_traits,
        server_config
      >;

    using combined_id = typename super::combined_id;
    using player_id = typename super::player_id;
    using session_id = typename super::session_id;
 
    using json = typename super::json;
    using session_data = typename matchmaker::session_data;
    using clock = typename super::clock;

  // main class body
  public:
    matchmaking_server(
        const jwt::verifier<jwt_clock, json_traits>& v,
        function<std::string(const combined_id&, const json&)> f
      ) : super{v, f, 30s} {}

    matchmaking_server(
        const jwt::verifier<jwt_clock, json_traits>& v,
        function<std::string(const combined_id&, const json&)> f,
        std::chrono::milliseconds t
      ) : super{v, f, t} {}

    void stop() {
      super::stop();
      {
        lock_guard<mutex> guard(m_match_lock);
        m_session_data.clear();
        m_altered_sessions.clear();
      }
      m_match_cond.notify_all();
    }

    void match_players(std::chrono::milliseconds timestep) {
      auto time_start = clock::now();
      while(super::is_running()) {
        unique_lock<mutex> lock(m_match_lock);

        while(!m_matchmaker.can_match(
            m_session_data, m_altered_sessions
          ))
        {
          m_match_cond.wait(lock);
          if(!super::is_running()) {
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
              super::complete_session(sid, g.session, g.data);
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
      super::process_message(id, data);

      unique_lock<mutex> lock(m_match_lock);
      auto session_data_it = m_session_data.find(id.session);
      if(session_data_it != m_session_data.end()) {
        json cancel_msg = m_matchmaker.get_cancel_data(
            id.session, session_data_it->second
          );

        m_altered_sessions.insert(id.session);
        m_session_data.erase(session_data_it);

        lock.unlock();

        super::complete_session(
            id.session,
            id.session,
            cancel_msg
          );
      }
    }

    bool player_connect(const combined_id& id, const json& data) {
      session_data d{data};
      if(!d.is_valid()) {
        spdlog::debug("error with player json: {}", data.dump());
        return false;
      }

      super::player_connect(id, data);

      {
        lock_guard<mutex> guard(m_match_lock);
        if(m_session_data.count(id.session) == 0) {
          m_session_data.emplace(std::make_pair(id.session, d));
          m_altered_sessions.insert(id.session);
        }
      }

      m_match_cond.notify_one();
      return true;
    }

    void player_disconnect(const combined_id& id) {
      super::player_disconnect(id);

      unique_lock<mutex> lock(m_match_lock);
      auto session_data_it = m_session_data.find(id.session);
      if(session_data_it != m_session_data.end()) {
        json cancel_msg = m_matchmaker.get_cancel_data(
            id.session, session_data_it->second
          );

        m_altered_sessions.insert(id.session);
        m_session_data.erase(session_data_it);

        lock.unlock();

        super::complete_session(
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

    map<session_id, session_data> m_session_data;
    set<session_id> m_altered_sessions;
  };
}

#endif // JWT_GAME_SERVER_GAME_SERVER_HPP
