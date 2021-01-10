// Implements the minimal interface for game and matchmaking classes

#ifndef MINIMAL_GAME_HPP
#define MINIMAL_GAME_HPP

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <vector>
#include <set>
#include <map>
#include <queue>

using std::vector;
using std::map;
using std::set;
using std::queue;

struct minimal_player_traits {
  using player_id = unsigned long;
  static player_id parse_id(const json& id_json) {
    return id_json.get<player_id>();
  }
};

class minimal_game {
public:
  using player_traits = minimal_player_traits;
  using player_id = player_traits::player_id;

  struct message {
    message(player_id i, const std::string& t) : id(i), text(t) {}

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
  
  void connect(player_id id) {}

  void disconnect(player_id id) {}

  void player_update(player_id id, const json& data) {
    for(player_id pid : m_player_list) {
      m_message_queue.push(message{pid, data.dump()});
    }
  }

  void game_update(long delta_time) {}

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

class minimal_matchmaker {
public:
  using player_traits = minimal_player_traits;
  using player_id = player_traits::player_id;

  struct player_data {
    player_data() {}
    player_data(const json& data) {}
  };

  struct game {
    game(const vector<player_id>& pl) : player_list(pl) {
      data["players"] = player_list;
    }

    vector<player_id> player_list;
    json data;
  };

  bool can_match(const map<player_id, player_data>& player_map,
      const set<player_id>& altered_players) {
    return player_map.size() >= 2;
  }

  vector<game> match(const map<player_id, player_data>& player_map,
      const set<player_id>& altered_players) {
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

  // called if a matchmade game was cancelled
  void cancel_game(const game& g) {}
};

#endif // MINIMAL_GAME_HPP
