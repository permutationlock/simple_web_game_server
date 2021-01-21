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
    using player_data = typename matchmaker::player_data;
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
      m_match_cond.notify_all();
    }

    void match_players(std::chrono::milliseconds timestep) {
      auto time_start = clock::now();
      while(super::is_running()) {
        unique_lock<mutex> lock(m_match_lock);

        while(!m_matchmaker.can_match(m_player_data, m_altered_players)) {
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

          {
            auto pd_copy{m_player_data};
            auto ap_copy{m_altered_players};
            m_altered_players.clear();

            lock.unlock();

            games = m_matchmaker.match(pd_copy, ap_copy);
          }

          for(const auto& g : games) {
            spdlog::trace("matched game: {}", g.data.dump());
            for(const combined_id& id : g.player_list) {
              super::complete_connection(id, {id.player, g.session}, g.data);
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
      {
        lock_guard<mutex> guard(m_match_lock);
        super::process_message(id, data);
        if(m_player_data.count(id) > 0) {
          super::complete_connection(
              id, id,
              m_matchmaker.get_cancel_data(
                  id, m_player_data.find(id)->second
                )
            );
          m_player_data.erase(id);
          m_altered_players.insert(id);
        }
      }
    }

    bool player_connect(const combined_id& id, const json& data) {
      player_data d{data};
      if(!d.is_valid()) {
        spdlog::debug("error with player json: {}", data.dump());
        return false;
      }

      {
        lock_guard<mutex> guard(m_match_lock);
        super::player_connect(id, data);
        m_player_data.emplace(std::make_pair(id, d));
        m_altered_players.insert(id);
      }

      m_match_cond.notify_one();
      return true;
    }

    void player_disconnect(const combined_id& id) {
      {
        lock_guard<mutex> guard(m_match_lock);
        super::player_disconnect(id);
        if(m_player_data.count(id) > 0) {
          m_player_data.erase(id);
          m_altered_players.insert(id);
        }
      }
    }

    // member variables
    matchmaker m_matchmaker;

    // m_match_lock guards the members m_player_data and m_altered_players
    mutex m_match_lock;
    condition_variable m_match_cond;

    map<combined_id, player_data> m_player_data;
    set<combined_id> m_altered_players;
  };
}

#endif // JWT_GAME_SERVER_GAME_SERVER_HPP
