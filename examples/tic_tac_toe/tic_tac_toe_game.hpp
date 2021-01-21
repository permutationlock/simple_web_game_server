#ifndef TIC_TAC_TOE_HPP
#define TIC_TAC_TOE_HPP

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <vector>
#include <map>
#include <queue>

using std::vector;
using std::set;
using std::map;
using std::queue;

const int X_VAL = 1;
const int O_VAL = -1;
const int EMPTY_VAL = 0;

class tic_tac_toe_board {
public:
  tic_tac_toe_board(): m_state(0), m_move_count(0) {
    for(std::size_t i=0; i<9; i++) {
      m_board.push_back(0);
    }
  }

  bool add_x(unsigned int i, unsigned int j) {
    if(i > 2 || j > 2) {
      return false;
    } else if(get_value(i, j) != EMPTY_VAL) {
      return false;
    }

    move(i, j, X_VAL);
    return true;
  }

  bool add_o(unsigned int i, unsigned int j) {
    if(i > 2 || j > 2) {
      return false;
    } else if(get_value(i, j) != EMPTY_VAL) {
      return false;
    }

    move(i, j, O_VAL);
    return true;
  }

  int get_state() const {
    return m_state;
  }

  bool is_done() const {
    return (m_move_count == 9) || (get_state() != 0);
  }

  const std::vector<int>& get_board() const {
    return m_board;
  }
  
private:
  int get_value(int i, int j) {
    return m_board[i+3*j];
  }

  void set_value(int i, int j, int s) {
    m_board[i+3*j] = s;
  }

  void move(int x, int y, int s){
    set_value(x, y, s);
    m_move_count++;

    //check col
    for(std::size_t i = 0; i < 3; i++){
      if(get_value(x, i) != s) {
        break;
      }
      if(i == 2){
        m_state = s;
        return;
      }
    }

    //check row
    for(std::size_t i = 0; i < 3; i++){
      if(get_value(i, y) != s) {
        break;
      }
      if(i == 2){
        m_state = s;
        return;
      }
    }

    //check diagonal
    if(x == y){
      for(std::size_t i = 0; i < 3; i++){
        if(get_value(i, i) != s) {
          break;
        }
        if(i == 2){
          m_state = s;
          return;
        }
      }
    }

    //check anti-diagonal
    if(x + y == 2){
      for(std::size_t i = 0; i < 3; i++){
        if(get_value(i, 2-i) != s) {
          break;
        }
        if(i == 2){
          m_state = s;
          return;
        }
      }
    }
  }

  // member variables
  vector<int> m_board;
  int m_state;
  std::size_t m_move_count;
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
    m_game_over(false), m_xmove(true), m_state(0)
  {
    spdlog::trace(msg.dump());

    try {
      m_player_list = msg.at("players").get<vector<player_id> >();
      m_xtime = msg.at("time").get<unsigned int>();
      m_otime = m_xtime;
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

      send(id, get_game_state(id));

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

    bool done = true;
    for(player_id player : m_player_list) {
      if(m_data_map[player].is_connected) {
        done = false;
      }
    }

    if(done) {
      m_game_over = true;
    }
  }

  void player_update(player_id id, const json& data) {
    try {
      unsigned int i = data["move"][0].get<unsigned int>();
      unsigned int j = data["move"][1].get<unsigned int>();

      if(m_started && !is_done()) {
        if(id == m_player_list[0]) {
          if(m_xmove) {
            if(m_board.add_x(i, j)) {
              m_xmove = false;
              m_move_list.push_back(data["move"]);
              for(player_id player : m_player_list) {
                send(player, get_game_state(player));
              }
            } else {
              spdlog::debug("player {} sent invalid move: {}", id, data.dump());
            }
          } else {
            spdlog::debug("player {} sent move out of turn: {}", id, data.dump());
          }
        } else {
          if(!m_xmove) {
            if(m_board.add_o(i, j)) {
              m_xmove = true;
              m_move_list.push_back(data["move"]);
              for(player_id player : m_player_list) {
                send(player, get_game_state(player));
              }
            } else {
              spdlog::debug("player {} sent invalid move: {}", id, data.dump());
            }
          } else {
            spdlog::debug("player {} sent move out of turn: {}", id, data.dump());
          }
        }
      }
    } catch(json::exception& e) {
      spdlog::error("player {} sent invalid json", id);
    }
  }

  void game_update(long delta_time) {
    if(m_started && !m_game_over) {
      if(m_xmove) {
        m_xtime -= delta_time;
      } else {
        m_otime -= delta_time;
      }

      if(m_xtime <= 0) {
        m_xtime = 0;
        m_state = -1;
        m_game_over = true;
      } else if(m_otime <= 0) {
        m_otime = 0;
        m_state = 1;
        m_game_over = true;
      }

      for(player_id player : m_player_list) {
        send(player, get_time_state(player));
        if(m_game_over) {
          send(player, get_game_state(player));
        }
      }
    }
  }

  json get_state(player_id id) const {
    bool isx = (id == m_player_list.front());

    json game_json;
    game_json["type"] = "game";
    game_json["board"] = m_board.get_board();
    game_json["players"] = m_player_list;  
    game_json["xmove"] = m_xmove;
    game_json["moves"] = m_move_list;
    game_json["times"] = std::vector<long>{ m_xtime, m_otime };
    game_json["state"] = m_board.get_state() + m_state;
    game_json["done"] = is_done();
    game_json["your_turn"] = isx ? m_xmove : !m_xmove;
 
    return game_json;
  }

  bool is_done() const {
    return m_board.is_done() || m_game_over;
  }
  
  bool is_valid() const {
    return m_valid;
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
  void send(player_id id, const json& msg) {
    m_message_queue.push(message{id, msg.dump()});
  }

  void start() {
     m_started = true;
  }

  json get_game_state(player_id id) const {
    bool isx = (id == m_player_list.front());

    json game_json{get_state(id)};
    game_json["your_turn"] = isx ? m_xmove : !m_xmove;
 
    return game_json;
  }

  json get_time_state(player_id id) const {
    bool isx = (id == m_player_list.front());

    json game_json;

    game_json["type"] = "time";
    game_json["your_time"] = (isx ? m_xtime : m_otime);
    game_json["opp_time"] = (isx ? m_otime : m_xtime); 
    return game_json;
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
  bool m_game_over;
  bool m_xmove;
  int m_state;
  long m_xtime;
  long m_otime;

  vector<json> m_move_list;

  tic_tac_toe_board m_board;
};

struct tic_tac_toe_matchmaking_data {
public: 
  using player_traits = tic_tac_toe_player_traits;
  using player_id = tic_tac_toe_player_traits::player_id;

  struct player_data {
    player_data() {}
    player_data(const json& data) {}
  };

  struct game {
    game(const vector<player_id>& pl) : player_list(pl) { 
      data["players"] = player_list;
      data["time"] = 10000;
    }

    vector<player_id> player_list;
    json data;
  };

  bool can_match(const map<player_id, player_data>& player_map,
      const set<player_id>& altered_players) {
    return player_map.size() >= 2;
  }

  vector<game> match(const map<player_id, player_data>& player_map,
      const set<player_id>& altered_players, long delta_time) {
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

  json get_cancel_data(player_id id, const player_data& data) {
    return game{std::vector<player_id>{}}.data;
  }
};

#endif // TIC_TAC_TOE_HPP