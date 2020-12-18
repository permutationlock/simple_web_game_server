#ifndef JWT_GAME_SERVER_GAME_SERVER_HPP
#define JWT_GAME_SERVER_GAME_SERVER_HPP

#include "base_server.hpp"

#include <set>
#include <chrono>

namespace jwt_game_server {
  // time literals to initialize timestep variables
  using namespace std::chrono_literals;

  // datatype implementations
  using std::set;
  using std::shared_ptr;
  using std::make_shared;

  template<typename game_data, typename json_traits, typename server_config>
  class game_instance {
  // type definitions
  private:
    using ws_server = websocketpp::server<server_config>;
    using player_id = typename game_data::player_id;

  // main class body
  public:
    game_instance(ws_server* s, const game_data& data) :
      m_server_ptr(s), m_game(data)  {}

    void connect(player_id id, connection_hdl hdl) {
      spdlog::trace("connect called for player {}", id);
      lock_guard<mutex> guard(m_game_lock);
      m_player_connections[id] = hdl;
      if(m_player_status.count(id) < 1 || !m_player_status[id]) {
        m_player_status[id] = true;
        m_game.connect(id);
      }
    }

    void disconnect(player_id id) {
      spdlog::trace("disconnect called for player {}", id);
      lock_guard<mutex> guard(m_game_lock);
      m_player_status[id] = false;
      m_game.disconnect(id);
    }

    bool is_connected(player_id id) {
      lock_guard<mutex> guard(m_game_lock);
      return m_player_status[id];
    }

    connection_hdl get_connection(player_id id) {
      lock_guard<mutex> guard(m_game_lock);
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

      lock_guard<mutex> guard(m_game_lock);
      m_game.player_update(id, msg_json);
      send_messages();
    }
    
    // update game every timestep, return false if game is over
    // this method should only be called by the game update thread
    bool game_update(long delta_time) {
      spdlog::trace("update with timestep: {}", delta_time);
      
      lock_guard<mutex> guard(m_game_lock);
      m_game.game_update(delta_time);
      send_messages();

      return m_game.is_done();
    }

    // return player ids
    vector<player_id> get_player_list() {
      lock_guard<mutex> guard(m_game_lock);
      return m_game.get_player_list();
    }

  private:
    // private members don't lock since public members already acquire the lock
    void broadcast(const std::string& text) {
      for (auto const& player : m_player_connections) {
        player_id id = player.first;
        connection_hdl hdl = player.second;
        if(m_player_status[id]) {
          try {
            m_server_ptr->send(hdl, text,
              websocketpp::frame::opcode::text);
          } catch (std::exception& e) {
            spdlog::debug(
                "error sending message \"{}\" to player {}: {}",
                text,
                id,
                e.what()
              );
          }
        }
      }
    }

    bool send(player_id id, const std::string& text) {
      bool success = true;
      if(m_player_status[id]) {
        try {
          m_server_ptr->send(
              m_player_connections[id],
              text,
              websocketpp::frame::opcode::text
            );
        } catch (std::exception& e) {
            spdlog::debug(
                "error sending message \"{}\" to player {}: {}",
                text,
                id,
                e.what()
              );
        }
      } else {
        success = false;
      }
      return success;
    }

    void send_messages() {
      spdlog::trace("sending queued messages");
      while(m_game.has_message()) {
        auto msg = m_game.get_message();
        if(msg.broadcast) {
          broadcast(msg.text);
        } else {
          send(msg.id, msg.text);
        }
        m_game.pop_message();
      }
    }

    // member variables
    ws_server* m_server_ptr;
    map<player_id, connection_hdl> m_player_connections;
    map<player_id, bool> m_player_status;
    mutex m_game_lock;
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

  // main class body
  public:
    game_server(const jwt::verifier<jwt_clock, json_traits>& v)
      : super(v), m_timestep(500ms) {}

    game_server(const jwt::verifier<jwt_clock, json_traits>& v,
      std::chrono::milliseconds t)
        : super(v), m_timestep(t) {}

    void update_games() {
      auto time_start = std::chrono::high_resolution_clock::now();
      while(super::m_is_running) { 
        auto delta_time =
          std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - time_start);
        if(delta_time >= m_timestep) {
          lock_guard<mutex> game_guard(m_game_list_lock);
          time_start = std::chrono::high_resolution_clock::now();
          
          // consider using std::execution::par_unseq here !!
          for(auto it = m_games.begin(); it != m_games.end();) {
            shared_ptr<
                game_instance<game_data, json_traits, server_config>
              > game = *it;

            if(game->game_update(delta_time.count())) {
              spdlog::debug("game ended");
              vector<player_id> ids(game->get_player_list());
              for(player_id id : ids) {
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
      m_player_games[id]->process_player_update(id, text);
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
            game_instance<game_data, json_traits, server_config>
          >(&(super::m_server), d);
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

    // member variables
    std::chrono::milliseconds m_timestep;
    mutex m_game_list_lock;

    map<
        player_id,
        shared_ptr<game_instance<game_data, json_traits, server_config> >
      > m_player_games;
    set<
        shared_ptr<game_instance<game_data, json_traits, server_config> >
      > m_games;
  };
}

#endif // JWT_GAME_SERVER_GAME_SERVER_HPP
