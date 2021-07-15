// Implements the minimal interface for game and matchmaking classes

#ifndef MINIMAL_GAME_HPP
#define MINIMAL_GAME_HPP

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <vector>
#include <unordered_set>
#include <unordered_map>

using std::vector;
using std::unordered_map;
using std::unordered_set;

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

    template<typename value>
    using map = std::unordered_map<id, value, hash>;

    template<typename value>
    using session_id_map = std::unordered_map<session_id, value, hash>;

    id() : player(0), session(0) {}
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
  using message = std::pair<player_id, std::string>;

  minimal_game(const json& data) {}
  
  void connect(vector<message>& out_messages, player_id id) {
    m_player_list.insert(id);
  }

  void disconnect(vector<message>& out_messages, player_id id) {
    m_player_list.erase(id);
  }

  void update(
      vector<message>& out_messages,
      const vector<message>& in_messages,
      long delta_time
    )
  {
    for(const message& msg : in_messages) {
      for(player_id pid : m_player_list) {
        json temp = { { "pid", msg.first }, { "message", msg.second } };
        out_messages.emplace_back(pid, temp.dump());
      }
    }
  }

  bool is_done() const {
    return m_player_list.empty();
  }

  bool is_valid() const {
    return true;
  }

  json get_state() const {
    json data;
    data["valid"] = true;
    return data;
  }
private:
  unordered_set<player_id> m_player_list;
};

class minimal_matchmaker {
public:
  using player_traits = minimal_player_traits;
  using session_id = player_traits::id::session_id;
  using message = std::pair<session_id, std::string>;
  using game = std::tuple<std::vector<session_id>, session_id, json>;

  struct session_data {
    session_data(const json& data) {}

    bool is_valid() {
      return true;
    }
  };

  using session_data_map = typename player_traits::id::session_id_map<session_data>;

  minimal_matchmaker() : m_sid_count(0) {}

  bool can_match(const session_data_map& session_map) {
    return session_map.size() > 1;
  }

  void match(
      vector<game>& game_list,
      vector<message>& out_messages,
      const session_data_map& session_map,
      long delta_time
    )
  {
    vector<session_id> sl;
    for(auto& spair : session_map) {
      sl.push_back(spair.first);
      if(sl.size() > 1) {
        game_list.emplace_back(
            std::move(sl), 
            m_sid_count++,
            json{ { "matched", true } }
          );
      }
    }
  }

  json get_cancel_data() const {
    json temp;
    temp["matched"] = false;
    return temp; 
  }

private:
  session_id m_sid_count;
};

#endif // MINIMAL_GAME_HPP
