// Implements the minimal interface for game and matchmaking classes

#ifndef MINIMAL_GAME_HPP
#define MINIMAL_GAME_HPP

#include <nlohmann/json.hpp>
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

  minimal_game(const json& msg) {}
  
  void connect(player_id id) {
    m_player_list.insert(id);
  }

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
    return true;
  }

  json get_state() const {
    json data;
    data["done"] = true;
    data["players"] = m_player_list;
    return data;
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
  set<player_id> m_player_list;
  queue<message> m_message_queue;
};

class minimal_matchmaker {
public:
  using player_traits = minimal_player_traits;
  using session_id = player_traits::id::session_id;

  struct session_data {
    session_data(const json& data) {}

    bool is_valid() {
      return true;
    }
  };

  struct game {
    game(const vector<session_id>& sl, session_id sid) : session_list(sl),
      session(sid)
    {
      data["matched"] = true;
    }

    vector<session_id> session_list;
    session_id session;
    json data;
  };

  minimal_matchmaker() : m_sid_count(0) {}

  bool can_match(
      const map<session_id, session_data>& session_map,
      const set<session_id>& altered_sessions
    )
  {
    return session_map.size() >= 2;
  }

  void match(
      vector<game>& game_list,
      const map<session_id, session_data>& session_map,
      const set<session_id>& altered_sessions
    )
  {
    vector<session_id> sl;
    for(auto& spair : session_map) {
      sl.push_back(spair.first);
      if(sl.size() > 1) {
        game_list.push_back(game{sl, m_sid_count++});
        sl.clear();
      }
    }
  }
 
  json get_cancel_data(
      session_id sid,
      const session_data& data
    )
  {
    json temp;
    temp["matched"] = false;
    return temp; 
  }

private:
  session_id m_sid_count;
};

#endif // MINIMAL_GAME_HPP
