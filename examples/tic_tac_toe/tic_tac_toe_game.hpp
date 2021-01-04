#ifndef TIC_TAC_TOE_HPP
#define TIC_TAC_TOE_HPP

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <vector>
#include <map>
#include <queue>

using std::vector;
using std::map;
using std::queue;

const int X_VAL = 1;
const int O_VAL = -1;

class tic_tac_toe_board {
public:
  tic_tac_toe_board(): m_state(0), m_done(false) {
    for(std::size_t i=0; i<9; i++) {
      m_board.push_back(0);
    }
  }

  bool add_x(unsigned int i, unsigned int j) {
    if(i > 2 || j > 2) {
      return false;
    } else if(m_board[i+3*j] != 0) {
      return false;
    }

    m_board[i+3*j] = X_VAL;
    update_state();
    return true;
  }

  bool add_o(unsigned int i, unsigned int j) {
    if(i > 2 || j > 2) {
      return false;
    } else if(m_board[i+3*j] != 0) {
      return false;
    }

    m_board[i+3*j] = O_VAL;
    update_state();
    return true;
  }

  int get_state() const {
    return m_state;
  }

  bool is_done() const {
    return m_done;
  }

  json to_json() const {
    return json{m_board};
  }
  
private:
  bool get_value(unsigned int i, unsigned int j) {
    return m_board[i+3*j];
  }

  void update_state() {
    m_done = true;
    for(int val : m_board) {
      if(val != 0) {
        m_done = false;
      }
    }
    
    // check columns
    for(std::size_t i = 0; i < 3; i++) {
      bool line = true;
      int last = 0;
      for(std::size_t j = 0; j < 3; j++) {
        if(j > 0 && last != get_value(i, j)) {
          line = false;
          break;
        }
        last = get_value(i, j);
      }

      if(line && last != 0) {
        m_state = last;
        return;
      }
    }

    // check rows
    for(std::size_t j = 0; j < 3; j++) {
      bool line = true;
      int last = 0;
      for(std::size_t i = 0; i < 3; i++) {
        if(j > 0 && last != get_value(i, j)) {
          line = false;
          break;
        }
        last = get_value(i, j);
      }

      if(line && last != 0) {
        m_state = last;
        return;
      }
    }

    // check diagonals
    if(get_value(0, 0) == get_value(1, 1)
        && get_value(1, 1)  == get_value(2, 2)) {
      if(get_value(1, 1) != 0) {
        m_state = get_value(1, 1);
        return;
      }
    }
    if(get_value(2, 0) == get_value(1, 1)
        && get_value(1, 1) == get_value(0, 2)) {
      if(get_value(1, 1) != 0) {
        m_state = get_value(1, 1);
        return;
      }
    }
    m_state = 0;
  }

  vector<int> m_board;
  int m_state;
  bool m_done;
};

struct tic_tac_toe_player_traits {
  typedef unsigned long player_id;
  static player_id parse_id(const json& id_json) {
    return id_json.get<player_id>();
  }
};

class tic_tac_toe_game {
public:
  typedef tic_tac_toe_player_traits player_traits;
  typedef player_traits::player_id player_id;

  struct message {
    message(player_id i, const std::string& t) : id(i), text(t) {}

    player_id id;
    std::string text;
  };

  tic_tac_toe_game(const json& msg) : m_valid(true), m_started(false),
    m_elapsed_time(0)
  {
    spdlog::trace(msg.dump());

    try {
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

      send(id, get_game_state());

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
      unsigned int i = data["move"][0].get<unsigned int>();
      unsigned int j = data["move"][1].get<unsigned int>();
      if(m_started) {
        if(id == m_player_list[0]) {
          if(m_xmove) {
            if(m_board.add_x(i, j)) {
              m_xmove = false;
              m_move_list.push_back(data["move"]);
              broadcast(get_game_state());
            } else {
              spdlog::debug("player sent invalid move: {}", data.dump());
            }
          }
        } else {
          if(!m_xmove) {
            if(m_board.add_o(i, j)) {
              m_xmove = true;
              m_move_list.push_back(data["move"]);
              broadcast(get_game_state());
            } else {
              spdlog::debug("player sent invalid move: {}", data.dump());
            }
          }
        }
      }
    } catch(json::exception& e) {
      spdlog::error("player {} sent invalid json", id);
    }
  }

  void game_update(long delta_time) {
    m_elapsed_time += delta_time;

    if(m_started) {
      //json update = { "time", m_elapsed_time };
      //broadcast(update);
    } else if(m_elapsed_time >= 20000) {
      // start the game after 20 seconds no matter what
      start();
    }
  }

  bool is_done() const {
    return m_board.is_done();
  }
  
  bool is_valid() const {
    return m_valid;
  }

  json get_game_state() const {
    json game_json;

    game_json["board"] = m_board.to_json();
    game_json["players"] = m_player_list;  
    game_json["xmove"] = m_xmove;
    game_json["moves"] = m_move_list;
    game_json["state"] = m_board.get_state();
    game_json["done"] = m_board.is_done();
 
    return game_json;
  }

  const vector<player_id>& get_player_list() const {
    return m_player_list;
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
    for(player_id id : m_player_list) {
      m_message_queue.push(message{id, msg.dump()});
    }
  }

  void send(player_id id, const json& msg) {
    m_message_queue.push(message{id, msg.dump()});
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

  vector<player_id> m_player_list;
  map<player_id, player_data> m_data_map;

  queue<message> m_message_queue;

  bool m_valid;
  bool m_started;
  bool m_xmove;
  long m_elapsed_time;

  vector<json> m_move_list;

  tic_tac_toe_board m_board;
};

struct tic_tac_toe_matchmaking_data {
public: 
  typedef tic_tac_toe_player_traits player_traits;
  typedef tic_tac_toe_player_traits::player_id player_id;

  struct player_data {
    player_data() {}
    player_data(const json& data) {}
  };

  struct game {
    game(const vector<player_id>& pl) : player_list(pl) { 
      data["board"] = tic_tac_toe_board{}.to_json();
      data["players"] = player_list;  
      data["xmove"] = true;
      data["moves"] = std::vector<json>{};
      data["state"] = 0;
      data["done"] = false;
    }

    vector<player_id> player_list;
    json data;
  };

  static bool can_match(const map<player_id, player_data>& player_map) {
    return player_map.size() >= 2;
  }

  static vector<game> match(const map<player_id, player_data>& player_map) {
    vector<player_id> pl;
    vector<game> game_list;

    for(auto const& player : player_map) {
      pl.push_back(player.first);
      if(pl.size() > 1) {
        game_list.push_back(game{pl});
        pl.clear();
      }
    }

    return game_list;
  }
};

#endif // TIC_TAC_TOE_HPP
