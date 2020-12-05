#ifndef JWT_GAME_SERVER_GAME_SERVER_HPP
#define JWT_GAME_SERVER_GAME_SERVER_HPP

#include "base_server.hpp"

#include <set>
#include <chrono>

namespace jwt_game_server {
  // Time literals to initialize timestep variables
  using namespace std::chrono_literals;

  // Define data type implementations
  using std::set;
  using std::shared_ptr;
  using std::make_shared;

  template<typename game_data, typename json_traits>
  class game_instance {
  public:
    typedef typename game_data::player_id player_id;

    game_instance(wss_server* s, const game_data& data) :
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
          m_server_ptr->send(hdl, text,
              websocketpp::frame::opcode::text);
        }
      }
    }

    bool send(player_id id, const std::string& text) {
      bool success = true;
      if(m_player_status[id]) {
        m_server_ptr->send(
            m_player_connections[id],
            text,
            websocketpp::frame::opcode::text
          );
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

    wss_server* m_server_ptr;
    map<player_id, connection_hdl> m_player_connections;
    map<player_id, bool> m_player_status;
    mutex m_game_lock;
    game_data m_game;
  };

  template<typename game_data, typename jwt_clock, typename json_traits>
  class game_server : public base_server<typename game_data::player_traits,
      jwt_clock, json_traits> {
  private:
    typedef base_server<typename game_data::player_traits, jwt_clock,
      json_traits> super;
    typedef typename super::player_id player_id;
    typedef typename super::json json;

    std::chrono::milliseconds m_timestep;

    mutex m_game_list_lock;

    map<player_id, shared_ptr<game_instance<game_data, json_traits> > >
      m_player_games;
    set<shared_ptr<game_instance<game_data, json_traits> > > m_games;

  public:
    game_server(const jwt::verifier<jwt_clock, json_traits>& v)
      : base_server<typename game_data::player_traits, jwt_clock,
        json_traits>(v), m_timestep(500ms) {}

    game_server(const jwt::verifier<jwt_clock, json_traits>& v,
      std::chrono::milliseconds t)
      : base_server<typename game_data::player_traits, jwt_clock,
        json_traits>(v), m_timestep(t) {}

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
        auto gs =
          make_shared<game_instance<game_data, json_traits> >(&(super::m_server), d);
        m_games.insert(gs);

        for(player_id id : d.get_player_list()) {
          m_player_games[id] = gs;
        }

        m_player_games[main_id]->connect(main_id, hdl);
      } else {
        if(m_player_games[main_id]->is_connected(main_id)) {
          lock_guard<mutex> connection_guard(super::m_connection_lock);
          connection_hdl hdl =
            m_player_games[main_id]->get_connection(main_id);
          super::m_connection_ids.erase(hdl);
          super::m_server.close(
              hdl,
              websocketpp::close::status::normal,
              "player connected again"
            );
          spdlog::debug("terminating redundant connection for player {}",
            main_id);
        }
        m_player_games[main_id]->connect(main_id, hdl);
      }
    }

    void player_disconnect(connection_hdl hdl) {
      player_id id;
      {
        lock_guard<mutex> connection_guard(super::m_connection_lock);
        id = super::m_connection_ids[hdl];
      }
      {
        lock_guard<mutex> game_guard(m_game_list_lock);
        m_player_games[id]->disconnect(id);
        m_player_games.erase(id);
      }
      
      super::player_disconnect(hdl);
    }

    void update_games() {
      auto time_start = std::chrono::high_resolution_clock::now();
      while(1) { 
        auto delta_time =
          std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - time_start);
        if(delta_time >= m_timestep) {
          lock_guard<mutex> guard(m_game_list_lock);
          time_start = std::chrono::high_resolution_clock::now();
          
          // consider using std::execution::par_unseq here !!
          for(auto it = m_games.begin(); it != m_games.end();) {
            shared_ptr<game_instance<game_data, json_traits> > game = *it;

            if(game->game_update(delta_time.count())) {
              it++;
            } else {
              spdlog::debug("game ended");
              vector<player_id> ids(game->get_player_list());
              for(player_id id : ids) {
                connection_hdl hdl = game->get_connection(id);

                super::m_server.close(
                    hdl,
                    websocketpp::close::status::normal,
                    "game ended"
                  );
              }
              spdlog::trace("erasing game from list");
              it = m_games.erase(it);
            }
          }
        }
        std::this_thread::sleep_for(std::min(1ms, m_timestep-delta_time));
      }
    }
  };
}

#endif // JWT_GAME_SERVER_GAME_SERVER_HPP
