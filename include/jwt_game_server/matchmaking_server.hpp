#ifndef JWT_GAME_SERVER_GAME_SERVER_HPP
#define JWT_GAME_SERVER_GAME_SERVER_HPP

#include "base_server.hpp"

#include <chrono>
#include <functional>

namespace jwt_game_server {
  // Time literals to initialize timestep variables
  using namespace std::chrono_literals;
  using std::function;

  template<typename matchmaking_data, typename jwt_clock, typename json_traits,
    typename server_config>
  class matchmaking_server : public base_server<
        typename matchmaking_data::player_traits,
        jwt_clock,
        json_traits,
        server_config
      > {
  // type definitions
  private:
    using super = base_server<
        typename matchmaking_data::player_traits,
        jwt_clock,
        json_traits,
        server_config
      >;
    using player_id = typename super::player_id;
    using json = typename super::json;
    using player_data = typename matchmaking_data::player_data;

  // main class body
  public:
    matchmaking_server(
        const jwt::verifier<jwt_clock, json_traits>& v,
        function<std::string(player_id, const json&)> f
      ) : super{v}, m_timestep{500ms}, m_sign_jwt{f} {}

    matchmaking_server(
        const jwt::verifier<jwt_clock, json_traits>& v,
        function<std::string(player_id, const json&)> f,
        std::chrono::milliseconds t
      ) : super{v}, m_timestep{t}, m_sign_jwt{f} {}


    void set_timestep(std::chrono::milliseconds t) {
      if(!super::is_running()) {
        m_timestep = t;
      }
    }

    void stop() {
      super::stop();
      m_match_cond.notify_all();
    }

    void match_players() {
      auto time_start = std::chrono::high_resolution_clock::now();
      while(super::is_running()) {
        unique_lock<mutex> lock(m_match_lock);

        while(!matchmaking_data::can_match(m_player_data)) {
          m_match_cond.wait(lock);
          if(!super::is_running()) {
            return;
          }
        }

        auto delta_time =
          std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - time_start);

        if(delta_time >= m_timestep) {
          vector<typename matchmaking_data::game> games
            = matchmaking_data::match(m_player_data);

          for(auto g : games) {
            spdlog::debug("matched game: {}", g.data);

            for(player_id id : g.player_list) {
              connection_hdl hdl = m_id_connections[id];

              lock.unlock();

              super::send_message(hdl, m_sign_jwt(id, g.data)); 

              super::close_connection(hdl, "matchmaking complete");

              lock.lock();
            }
          }

          time_start = std::chrono::high_resolution_clock::now();
        }

        lock.unlock();
        std::this_thread::sleep_for(std::min(1ms, m_timestep-delta_time));
      }
    }

  private:
    void process_message(player_id id, const std::string& text) {
      super::process_message(id, text);
    }

    void player_connect(connection_hdl hdl, player_id main_id,
        const json& data) {
      player_data d;
      try {
        d = player_data{data};
      } catch(std::exception& e) {
        spdlog::debug("error with player json: {}", e.what());
        return;
      }

      super::player_connect(hdl, main_id, data);

      {
        lock_guard<mutex> guard(m_match_lock);
        m_id_connections[main_id] = hdl;
        m_player_data[main_id] = d;
      }

      m_match_cond.notify_one();
    }

    player_id player_disconnect(connection_hdl hdl) {
      player_id id = super::player_disconnect(hdl);
      {
        lock_guard<mutex> connection_guard(m_match_lock);
        m_id_connections.erase(id);
        m_player_data.erase(id);
      }
      
      return id;
    }

    // member variables
    std::chrono::milliseconds m_timestep;

    mutex m_match_lock;
    condition_variable m_match_cond;

    function<std::string(player_id, const json&)> m_sign_jwt;

    map<player_id, connection_hdl> m_id_connections;
    map<player_id, player_data> m_player_data;
  };
}

#endif // JWT_GAME_SERVER_GAME_SERVER_HPP
