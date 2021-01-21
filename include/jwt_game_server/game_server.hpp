#ifndef JWT_GAME_SERVER_GAME_SERVER_HPP
#define JWT_GAME_SERVER_GAME_SERVER_HPP

#include "base_server.hpp"

#include <set>
#include <chrono>
#include <algorithm>
#include <execution>

namespace jwt_game_server {
  // time literals to initialize timestep variables
  using namespace std::chrono_literals;

  // datatype implementations
  using std::set;

  template<typename game_instance, typename jwt_clock, typename json_traits,
    typename server_config>
  class game_server : public base_server<typename game_instance::player_traits,
      jwt_clock, json_traits, server_config> {
  // type definitions
  private:
    using super = base_server<typename game_instance::player_traits, jwt_clock,
      json_traits, server_config>;

    using combined_id = typename super::combined_id;
    using player_id = typename super::player_id;
    using session_id = typename super::session_id;

    using json = typename super::json;
    using clock = typename super::clock;

  // main class body
  public:
    game_server(
        const jwt::verifier<jwt_clock, json_traits>& v,
        function<std::string(combined_id, const json&)> f
      ) : super(v, f, 30s) {}

    game_server(
        const jwt::verifier<jwt_clock, json_traits>& v,
        function<std::string(combined_id, const json&)> f,
        std::chrono::milliseconds t
      ) : super(v, f, t) {}

    void reset() {
      super::reset();

      lock_guard<mutex> guard(m_game_list_lock);
      m_games.clear();
    }

    void update_games(std::chrono::milliseconds timestep) {
      auto time_start = clock::now();
      while(super::is_running()) {
        const auto delta_time =
          std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now() - time_start
          );

        if(delta_time >= timestep) {
          lock_guard<mutex> game_guard(m_game_list_lock);
          time_start = clock::now();

          // game updates are completely independent, so exec in parallel
          std::for_each(
              std::execution::par,
              m_games.begin(),
              m_games.end(),
              [=](auto& key_val_pair){
                key_val_pair.second.game_update(delta_time.count());
              }
            );

          // base_server actions are locked so exec in sequence
          for(auto it = m_games.begin(); it != m_games.end();) {
            session_id sid = it->first;

            send_messages(sid);

            if(it->second.is_done()) {
              spdlog::debug("game session {} ended", sid);
              for(player_id id : it->second.get_player_list()) {
                combined_id cid{id, sid};
                super::complete_connection(cid, cid, it->second.get_state());
              }
              spdlog::trace("erasing game session {} from list", sid);
              it = m_games.erase(it);
            } else {
              it++;
            }
          }
        }
        std::this_thread::sleep_for(std::min(1ms, timestep-delta_time));
      }
    }

    std::size_t get_game_count() {
      lock_guard<mutex> guard(m_game_list_lock);
      return m_games.size();
    }

  private:
    void process_message(const combined_id& id, const json& data)
    {
      super::process_message(id, data);

      lock_guard<mutex> guard(m_game_list_lock);
      auto it = m_games.find(id.session);
      if(it != m_games.end()) {
        it->second.player_update(id.player, data);
        send_messages(id.session);
      }
    }

    bool player_connect(const combined_id& id, const json& data) {
      lock_guard<mutex> game_guard(m_game_list_lock);

      if(m_games.count(id.session) < 1) {
        game_instance game{data};

        if(!game.is_valid()) {
          spdlog::error("connection provided invalid game data");
          return false;
        }

        m_games.emplace(std::make_pair(id.session, game));
      }

      super::player_connect(id, data);

      m_games.find(id.session)->second.connect(id.player);
      send_messages(id.session);
      
      return true;
    }

    void player_disconnect(const combined_id& id) {
      {
        lock_guard<mutex> game_guard(m_game_list_lock);
        super::player_disconnect(id);
        auto it = m_games.find(id.session);
        if(it != m_games.end()) {
          it->second.disconnect(id.player);
        }
      }
    }

    // send all available messages for a given game (assumes m_game_lock
    // acquired)
    void send_messages(session_id sid) {
      using message = typename game_instance::message;
      auto game_it = m_games.find(sid);

      while(game_it->second.has_message()) {
        message msg = game_it->second.get_message();
        super::send_message({msg.id, sid}, msg.text);
        game_it->second.pop_message();
      }
    }

    // member variables
    map<
        session_id,
        game_instance
      > m_games;

    // m_game_list_lock guards the member m_games
    mutex m_game_list_lock;
  };
}

#endif // JWT_GAME_SERVER_GAME_SERVER_HPP
