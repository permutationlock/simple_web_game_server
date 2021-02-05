// Implements the minimal interface for game and matchmaking classes

#ifndef MINIMAL_GAME_HPP
#define MINIMAL_GAME_HPP

#include <nlohmann/json.hpp>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <functional>

using std::vector;
using std::unordered_map;
using std::unordered_set;
using std::queue;

struct minimal_player_traits {
  struct id {
    using player_id = unsigned long;
    using session_id = unsigned long;

    struct hash {
      std::size_t operator()(const id& id_data) const {
        return std::hash<player_id>{}(id_data.player)
          ^ std::hash<session_id>{}(id_data.session);
      }

      std::size_t operator()(unsigned long int_id) const {
        return std::hash<unsigned long>{}(int_id);
      }
    };

    id() {}
    id(player_id p, session_id s) : player(p), session(s) {}

    bool operator==(const id& other_id) const {
      return (player == other_id.player) && (session == other_id.session);
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
  
  void connect(vector<message>& msg_list, player_id id) {
    m_player_list.insert(id);
  }

  void disconnect(vector<message>& msg_list, player_id id) {}

  void player_update(
      vector<message>& msg_list, player_id id, const json& data
    )
  {
    for(player_id pid : m_player_list) {
      msg_list.emplace_back(pid, data.dump());
    }
  }

  void game_update(vector<message>& msg_list, long delta_time) {}

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

private:
  unordered_set<player_id> m_player_list;
};

class minimal_matchmaker {
public:
  using player_traits = minimal_player_traits;
  using session_id = player_traits::id::session_id;
  using hash_id = player_traits::id::hash;

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
      data["valid"] = true;
    }

    vector<session_id> session_list;
    session_id session;
    json data;
  };

  minimal_matchmaker() : m_sid_count(0) {}

  bool can_match(
      const unordered_map<session_id, session_data, hash_id>& session_map,
      const unordered_set<session_id, hash_id>& altered_sessions
    ) const
  {
    return session_map.size() >= 2;
  }

  void match(
      vector<game>& game_list,
      const unordered_map<session_id, session_data, hash_id>& session_map,
      const unordered_set<session_id, hash_id>& altered_sessions
    )
  {
    vector<session_id> sl;
    for(auto& spair : session_map) {
      sl.push_back(spair.first);
      if(sl.size() > 1) {
        game_list.emplace_back(sl, m_sid_count++);
        sl.clear();
      }
    }
  }

  json get_invalid_data() const {
    json temp;
    temp["matched"] = false;
    temp["valid"] = false;
    return temp; 
  }

  json get_cancel_data(
      const session_id& sid,
      const session_data& data
    ) const
  {
    json temp;
    temp["matched"] = false;
    temp["valid"] = true;
    return temp; 
  }

private:
  session_id m_sid_count;
};

#endif // MINIMAL_GAME_HPP
