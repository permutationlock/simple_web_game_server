#ifndef TIC_TAC_TOE_HPP
#define TIC_TAC_TOE_HPP

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <iostream>
#include <mutex>
#include <condition_variable>
#include <chrono>

using std::vector;
using std::map;
using std::pair;
using std::queue;

class tic_tac_toe_game {
public:
  typedef unsigned long player_id;

  struct message {
    message(bool b, player_id i, const std::string& t) : broadcast(b),
      id(i), text(t) {}

    bool broadcast;
    player_id id;
    std::string text;
  };

  tic_tac_toe_game(const json& msg) : m_valid(true), m_started(false),
    m_done(false), m_turn(0), m_elapsed_time(0)
  {
    spdlog::trace(msg.dump());

    try {
      m_creator_id = msg.at("creator_id").get<player_id>();
      m_player_list = msg.at("players").get<vector<player_id> >();
    } catch(json::exception& e) {
      m_valid=false;
    }

    for(player_id id : m_player_list) {
      m_data_map[id] = player_data{};
    }
  }
  
  void connect(player_id id) {
    if(m_data_map.count(id) < 1) {
      spdlog::debug("recieved connection from player {} not in list", id);
    } else {
      m_data_map[id].is_connected = true;

      if(!m_data_map[id].has_connected) {
        m_data_map[id].has_connected = true;
        
        bool ready = true;
        for(player_id id : m_player_list) {
          if(!m_data_map[id].has_connected) {
            ready = false;
            break;
          }
        }
        if(ready) {
          start();
        }
      }
    }
  }

  void disconnect(player_id id) {
    m_data_map[id].is_connected = false;
  }

  void player_update(player_id id, const json& data) {
    try {
      send(id, data);
    } catch(json::exception& e) {
      spdlog::error("player {} sent invalid json", id);
    }
  }

  void game_update(long delta_time) {
    m_elapsed_time += delta_time;

    if(m_started) {
      json update = { "time", m_elapsed_time };
      broadcast(update);
    } else if(m_elapsed_time >= 20000) {
      // start the game after 20 seconds no matter what
      start();
    }
  }

  bool is_done() const {
    return m_done;
  }
  
  bool is_valid() const {
    return m_valid;
  }

  json get_game_state() const {
    return json{};
  }

  const vector<player_id>& get_player_list() const {
    return m_player_list;
  }

  player_id get_creator_id() const {
    return m_creator_id;
  }

  bool has_message() const {
    return !m_message_queue.empty();
  }

  const message& get_message() const {
    return m_message_queue.front();
  }

  void pop_message() {
    m_message_queue.pop();
  }

private:
  void broadcast(const json& msg) {
    m_message_queue.push(message(true, 0, msg.dump()));
  }

  void send(player_id id, const json& msg) {
    m_message_queue.push(message(false, id, msg.dump()));
  }

  void start() {
     m_started = true;
     m_elapsed_time = 0;
  }

  struct player_data {
    player_data() : has_connected(false), is_connected(false) {}
    bool has_connected;
    bool is_connected;
  };

  player_id m_creator_id;
  vector<player_id> m_player_list;
  map<player_id, player_data> m_data_map;

  queue<message> m_message_queue;

  bool m_valid;
  bool m_started;
  bool m_done;
  unsigned int m_turn;
  long m_elapsed_time;
  unsigned int turn;
};

#endif // TIC_TAC_TOE_HPP
