// Implements the minimal interface for game and matchmaking classes

#ifndef MINIMAL_GAME_HPP
#define MINIMAL_GAME_HPP

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <vector>
#include <map>
#include <queue>

using std::vector;
using std::map;
using std::queue;

struct minimal_player_traits {
  typedef unsigned long player_id;
  static player_id parse_id(const json& id_json) {
    return id_json.get<player_id>();
  }
};

class minimal_game {
public:
  typedef minimal_player_traits player_traits;
  typedef player_traits::player_id player_id;

  struct message {
    message(bool b, player_id i, const std::string& t) : broadcast(b),
      id(i), text(t) {}

    bool broadcast;
    player_id id;
    std::string text;
  };

  minimal_game(const json& msg): m_valid(true) {
    try {
      m_player_list = msg.at("players").get<vector<player_id> >();
    } catch(json::exception& e) {
      m_valid=false;
    }
  }
  
  void connect(player_id id) {
  }

  void disconnect(player_id id) {
  }

  void player_update(player_id id, const json& data) {
  }

  void game_update(long delta_time) {
  }

  bool is_done() const {
    return true;
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
  vector<player_id> m_player_list;

  queue<message> m_message_queue;

  bool m_valid;
};

class minimal_matchmaking_data {
public: 
  typedef minimal_player_traits player_traits;
  typedef minimal_player_traits::player_id player_id;

  class player_data {
  public:
    player_data() {}
    player_data(const json& data) {}
  };

  class game {
  public:
    bool add_player(player_id id) {
      m_player_list.push_back(id);
      return (m_player_list.size() > 1);
    }

    json to_json() {
      json game_json;
      game_json["players"] = m_player_list;  

      return game_json;
    }

    const vector<player_id>& get_player_list() const {
      return m_player_list;
    }

  private:
    vector<player_id> m_player_list;
  };

  static vector<game> match(const map<player_id, player_data>& player_map) {
    vector<game> game_list;
    game g;
    for(auto const& player : player_map) {
      if(g.add_player(player.first)) {
        game_list.push_back(g);
        g = game{};
      }
    }

    return game_list;
  }
};

#endif // MINIMAL_GAME_HPP
