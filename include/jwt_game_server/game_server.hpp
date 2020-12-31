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
  using std::shared_ptr;
  using std::make_shared;

  template<typename game_data, typename json_traits>
  class game_instance {
  // type definitions
  private:
    using player_id = typename game_data::player_id;

  // main class body
  public:
    game_instance(const game_data& data) :
        m_game(data)  {
      for(player_id id : data.get_player_list()) {
        m_player_status[id] = false;
      }
    }

    void connect(player_id id, connection_hdl hdl) {
      spdlog::trace("connect called for player {}", id);
      m_player_connections[id] = hdl;
      if(m_player_status.count(id) < 1 || !m_player_status[id]) {
        m_player_status[id] = true;
        m_game.connect(id);
      }
    }

    void disconnect(player_id id) {
      spdlog::trace("disconnect called for player {}", id);
      m_player_status[id] = false;
      m_game.disconnect(id);
    }

    bool is_connected(player_id id) {
      return m_player_status[id];
    }

    connection_hdl get_connection(player_id id) {
      return m_player_connections[id];
    }

    void process_player_update(player_id id, const std::string& text) {  
      spdlog::trace("player_update called for player {}", id);
      typename json_traits::json msg_json;

      try {
        json_traits::parse(msg_json, text);
      } catch(std::exception& e) {
        spdlog::debug("update message from {} was not valid json", id);
        return;
      }

      m_game.player_update(id, msg_json);
    }
    
    // update game every timestep
    void game_update(long delta_time) {
      spdlog::trace("update with timestep: {}", delta_time);
      
      m_game.game_update(delta_time);
    }

    bool is_done() { 
      return m_game.is_done();
    }

    // return player ids
    const vector<player_id>& get_player_list() {
      return m_game.get_player_list();
    }

    struct message {
      connection_hdl hdl;
      std::string text;
    };

    bool get_message(message& msg) {
      if(!m_game.has_message()) {
        return false;
      }
      
      auto game_message = m_game.get_message();
      m_game.pop_message();

      if(!is_connected(game_message.id)) {
        spdlog::debug("message discarded for player {} who is not connected",
          game_message.id);
        return false;
      }

      msg.hdl = m_player_connections[game_message.id];
      msg.text = game_message.text;

      return true;
    }

  private:
    // member variables
    map<player_id, connection_hdl> m_player_connections;
    map<player_id, bool> m_player_status;
    game_data m_game;
  };

  template<typename game_data, typename jwt_clock, typename json_traits,
    typename server_config>
  class game_server : public base_server<typename game_data::player_traits,
      jwt_clock, json_traits, server_config> {
  // type definitions
  private:
    using super = base_server<typename game_data::player_traits, jwt_clock,
      json_traits, server_config>;
    using player_id = typename super::player_id;
    using json = typename super::json;
    using game_ptr = shared_ptr<game_instance<game_data, json_traits> >;

  // main class body
  public:
    game_server(const jwt::verifier<jwt_clock, json_traits>& v)
      : super(v), m_timestep(500ms) {}

    game_server(const jwt::verifier<jwt_clock, json_traits>& v,
      std::chrono::milliseconds t)
        : super(v), m_timestep(t) {}

    void set_timestep(std::chrono::milliseconds t) {
      if(!super::is_running()) {
        m_timestep = t;
      }
    }

    void update_games() {
      auto time_start = std::chrono::high_resolution_clock::now();
      while(super::is_running()) { 
        const auto delta_time =
          std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - time_start);
        if(delta_time >= m_timestep) {
          lock_guard<mutex> game_guard(m_game_list_lock);
          time_start = std::chrono::high_resolution_clock::now();

          // game updates are completely independent, so exec in parallel
          std::for_each(
              std::execution::par,
              m_games.begin(),
              m_games.end(),
              [=](game_ptr game){
                game->game_update(delta_time.count());
              }
            );

          // base_server actions are locked so exec in sequence
          for(auto it = m_games.begin(); it != m_games.end();) {
            game_ptr game = *it;

            send_messages(game);

            if(game->is_done()) {
              spdlog::debug("game ended");
              for(player_id id : game->get_player_list()) {
                connection_hdl hdl = game->get_connection(id);
                super::close_connection(hdl, "game ended");
              }
              spdlog::trace("erasing game from list");
              it = m_games.erase(it);
            } else {
              it++;
            }
          }
        }
        std::this_thread::sleep_for(std::min(1ms, m_timestep-delta_time));
      }
    }

  private:
    void process_message(player_id id, const std::string& text) {
      {
        lock_guard<mutex> guard(m_game_list_lock);
        m_player_games[id]->process_player_update(id, text);
        send_messages(m_player_games[id]);
      }
      super::process_message(id, text);
    }

    void player_connect(connection_hdl hdl, player_id main_id,
        const json& data) {
      game_data d(data);

      if(!d.is_valid()) {
        spdlog::debug("connection provided incorrect login json");
        return;
      }

      super::player_connect(hdl, main_id, data);

      lock_guard<mutex> game_guard(m_game_list_lock);
      if(m_player_games.count(main_id)<1) {
        auto gs = make_shared<
            game_instance<game_data, json_traits>
          >(d);
        m_games.insert(gs);

        for(player_id id : d.get_player_list()) {
          m_player_games[id] = gs;
        }

        m_player_games[main_id]->connect(main_id, hdl);
      } else {
        if(m_player_games[main_id]->is_connected(main_id)) {
          m_player_games[main_id]->disconnect(main_id);
          connection_hdl hdl =
            m_player_games[main_id]->get_connection(main_id);
          super::player_disconnect(hdl);
          
          super::close_connection(hdl, "player opened redundant connection");

          spdlog::debug("terminating redundant connection for player {}",
            main_id);
        }

        m_player_games[main_id]->connect(main_id, hdl);
      }

      send_messages(m_player_games[main_id]);
    }

    player_id player_disconnect(connection_hdl hdl) {
      player_id id = super::player_disconnect(hdl);
      {
        lock_guard<mutex> game_guard(m_game_list_lock);
        m_player_games[id]->disconnect(id);
        m_player_games.erase(id);
      }
      return id;
    }

    // send all available messages for a given game
    void send_messages(game_ptr game) {
      typename game_instance<game_data, json_traits>::message msg;
      while(game->get_message(msg)) {
        super::send_message(msg.hdl, msg.text);
      }
    }

    // member variables
    std::chrono::milliseconds m_timestep;
    mutex m_game_list_lock;

    map<
        player_id,
        game_ptr
      > m_player_games;
    set<
        game_ptr
      > m_games;
  };
}

#endif // JWT_GAME_SERVER_GAME_SERVER_HPP
