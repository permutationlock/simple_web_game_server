#ifndef JWT_GAME_SERVER_GAME_SERVER_HPP
#define JWT_GAME_SERVER_GAME_SERVER_HPP

#include "base_server.hpp"

#include <chrono>

namespace jwt_game_server {
  // Time literals to initialize timestep variables
  using namespace std::chrono_literals;

  template<typename matchmaking_data, typename jwt_clock, typename json_traits>
  class matchmaking_server : public base_server<
        typename matchmaking_data::player_traits,
        jwt_clock, json_traits
      > {
  private:
    typedef base_server<typename matchmaking_data::player_traits, jwt_clock,
      json_traits> super;
    typedef typename super::player_id player_id;
    typedef typename super::json json;
    typedef typename matchmaking_data::player_data player_data;

    std::chrono::milliseconds m_timestep;

    condition_variable m_match_cond;

    map<player_id, connection_hdl> m_id_connections;
    map<player_id, player_data> m_player_data;

    void process_message(player_id id, const std::string& text) {
      super::process_message(id, text);
    }

    void player_connect(connection_hdl hdl, player_id main_id,
        const json& data) {
      player_data d(data);

      super::player_connect(hdl, main_id, data);

      {
        lock_guard<mutex> guard(super::m_connection_lock);
        m_id_connections[main_id] = hdl;
        m_player_data[main_id] = d;
      }

      m_match_cond.notify_one();
    }

    void player_disconnect(connection_hdl hdl) {
      {
        lock_guard<mutex> connection_guard(super::m_connection_lock);
        player_id id = super::m_connection_ids[hdl];
        m_id_connections.erase(id);
        m_player_data.erase(id);
      }
      
      super::player_disconnect(hdl);
    }

  public:
    matchmaking_server(const jwt::verifier<jwt_clock, json_traits>& v)
      : base_server<typename matchmaking_data::player_traits, jwt_clock,
        json_traits>(v), m_timestep(500ms) {}

    matchmaking_server(const jwt::verifier<jwt_clock, json_traits>& v,
      std::chrono::milliseconds t)
      : base_server<typename matchmaking_data::player_traits, jwt_clock,
        json_traits>(v), m_timestep(t) {}

    void match_players() {
      auto time_start = std::chrono::high_resolution_clock::now();
      while(1) {
        auto delta_time =
          std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - time_start);

        if(delta_time >= m_timestep) {
          unique_lock<mutex> lock(super::m_connection_lock);
          while(m_player_data.size() < 2) {
            m_match_cond.wait(lock);
          }

          delta_time =
            std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::high_resolution_clock::now() - time_start);

          vector<typename matchmaking_data::game> games
            = matchmaking_data::match(m_player_data);

          for(auto game : games) {
            std::string text = (game.to_json()).dump();
            spdlog::debug("matched game with the following players:");
            for(player_id id : game.get_player_list()) {
              connection_hdl hdl = m_id_connections[id];
              super::m_server.send(hdl, text,
                  websocketpp::frame::opcode::text);
              
              lock.unlock();
              player_disconnect(hdl);
              lock.lock();

              super::m_server.close(
                  hdl,
                  websocketpp::close::status::normal,
                  "matchmaking complete"
                );

              spdlog::debug("  player {}", id);
            }
          }

          time_start = std::chrono::high_resolution_clock::now();
        }
        std::this_thread::sleep_for(std::min(1ms, m_timestep-delta_time));
      }
    }
  };
}

#endif // JWT_GAME_SERVER_GAME_SERVER_HPP
