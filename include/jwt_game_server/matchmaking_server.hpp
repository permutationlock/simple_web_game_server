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

  // functional types
  using std::function;

  template<typename matchmaker, typename jwt_clock, typename json_traits,
    typename server_config>
  class matchmaking_server : public base_server<
        typename matchmaker::player_traits,
        jwt_clock,
        json_traits,
        server_config
      > {
  // type definitions
  private:
    using super = base_server<
        typename matchmaker::player_traits,
        jwt_clock,
        json_traits,
        server_config
      >;
    using player_id = typename super::player_id;
    using json = typename super::json;
    using player_data = typename matchmaker::player_data;

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

        while(!m_matchmaker.can_match(m_player_data, m_altered_players)) {
          m_match_cond.wait(lock);
          if(!super::is_running()) {
            return;
          }
        }

        auto delta_time =
          std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - time_start);

        if(delta_time >= m_timestep) {
          vector<typename matchmaker::game> games;
          vector<typename matchmaker::game> cancelled_games;

          {
            auto pd_copy{m_player_data};
            auto ap_copy{m_altered_players};
            m_altered_players.clear();

            lock.unlock();

            games = m_matchmaker.match(pd_copy, ap_copy, delta_time.count());
          }

          for(const auto& g : games) {
            spdlog::debug("matched game: {}", g.data.dump());
            vector<connection_hdl> connections;
            bool players_connected = true;

            // get connections and see if any player has disconnected
            for(player_id id : g.player_list) {
              connection_hdl hdl;
              if(super::get_connection_hdl_from_id(id, hdl)) {
                connections.push_back(hdl);
              } else {
                spdlog::debug(
                    "player {} prematurely disconnected after being matched",
                    id
                  );
                players_connected = false;
              }
            }
            
            if(players_connected) {
              for(std::size_t i = 0; i < g.player_list.size(); i++) {
                player_id id = g.player_list[i];
                connection_hdl hdl = connections[i];

                super::send_message(hdl, m_sign_jwt(id, g.data)); 
                super::close_connection(hdl, "matchmaking complete");
              }
            } else {
              cancelled_games.push_back(g);
            }
          }

          lock.lock();
          m_altered_players.clear();
          m_matchmaker.cancel_games(cancelled_games);
          time_start = std::chrono::high_resolution_clock::now();
          spdlog::debug("matching completed with {}ms of timestep remaining",
            (m_timestep-delta_time).count());
        }

        lock.unlock();
        std::this_thread::sleep_for(std::min(1ms, m_timestep-delta_time));
      }
    }

  private:
    // proper procedure for client to cancel matchmaking is to send a message
    // and wait for the server to close the connection; by acquiring the match
    // lock and marking the user as unavailable we avoid the situation where a
    // player disconnects after the match function is called, but before a game
    // token is successfully sent to them
    void process_message(connection_hdl hdl, player_id id,
        const std::string& text) {
      {
        lock_guard<mutex> guard(m_match_lock);
        if(m_player_data.count(id) > 0) {
          super::close_connection(hdl, "matchmaking cancelled");
          m_player_data.erase(id);
          m_altered_players.insert(id);
        }
      }
      super::process_message(hdl, id, text);
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

      {
        lock_guard<mutex> guard(m_match_lock);
        super::player_connect(hdl, main_id, data);
        m_player_data[main_id] = d;
        m_altered_players.insert(main_id);
      }

      m_match_cond.notify_one();
    }

    player_id player_disconnect(connection_hdl hdl) {
      player_id id;
      {
        lock_guard<mutex> guard(m_match_lock);
        id = super::player_disconnect(hdl);
        m_player_data.erase(id);
        m_altered_players.insert(id);
      }
      
      return id;
    }

    // member variables
    matchmaker m_matchmaker;
    std::chrono::milliseconds m_timestep;

    // m_match_lock guards the members m_player_data and m_altered_players
    mutex m_match_lock;
    condition_variable m_match_cond;

    function<std::string(player_id, const json&)> m_sign_jwt;
    map<player_id, player_data> m_player_data;
    set<player_id> m_altered_players;
  };
}

#endif // JWT_GAME_SERVER_GAME_SERVER_HPP
