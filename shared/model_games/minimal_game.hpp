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
  struct id {
    using player_id = unsigned long;
    using session_id = unsigned long;

    id() {}
    id(player_id p, session_id s) : player(p), session(s) {}

    bool operator<(const id& other) const {
      if(player < other.player) {
        return true;
      } else if(other.player < player) {
        return false;
      } else if(session < other.session) {
        return true;
      } else {
        return false;
      }
    }
  
    player_id player;
    session_id session;
  };

  static id::player_id parse_player_id(const json& id_json) {
    return id_json.get<id::player_id>();
  }

  static id::session_id parse_session_id(const json& id_json) {
    return id_json.get<id::session_id>();
  }
};

class minimal_game {
public:
  using player_traits = minimal_player_traits;
  using player_id = player_traits::id::player_id;

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

  json get_state() const {
    json data;
    data["done"] = true;
    data["players"] = m_player_list;
    return data;
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
  using combined_id = player_traits::id;
  using player_id = combined_id::player_id;
  using session_id = combined_id::session_id;

  struct player_data {
    player_data() {}
    player_data(const json& data) {}

    bool is_valid() {
      return true;
    }
  };

  struct game {
    game(const vector<combined_id>& pl, session_id sid) : player_list(pl),
      session(sid)
    {
      std::vector<player_id> pid_list;
      for(const combined_id& id : player_list) {
        pid_list.push_back(id.player);
      }
      data["players"] = pid_list;
    }

    vector<combined_id> player_list;
    json data;
    session_id session;
  };

  minimal_matchmaker() : m_sid_count(0) {}

  bool can_match(const map<combined_id, player_data>& player_map,
      const set<combined_id>& altered_players) {
    return player_map.size() >= 2;
  }

  vector<game> match(const map<combined_id, player_data>& player_map,
      const set<combined_id>& altered_players) {
    vector<combined_id> pl;
    vector<game> game_list;

    for(auto& player : player_map) {
      pl.push_back(player.first);
      if(pl.size() > 1) {
        game_list.push_back(game{pl, m_sid_count++});
        pl.clear();
      }
    }

    return game_list;
  }
 
  json get_cancel_data(combined_id id, const player_data& data) {
    return game{std::vector<combined_id>{}, id.session}.data;
  }

private:
  session_id m_sid_count;
};

#endif // MINIMAL_GAME_HPP
