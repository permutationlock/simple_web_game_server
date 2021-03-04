// Implements a test interface for game and matchmaking classes

#ifndef MINIMAL_GAME_HPP
#define MINIMAL_GAME_HPP

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <spdlog/spdlog.h>

#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <functional>
#include <tuple>
#include <utility>

using std::vector;
using std::unordered_map;
using std::unordered_set;
using std::queue;

struct test_player_traits {
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

class test_game {
public:
  using player_traits = test_player_traits;
  using player_id = player_traits::id::player_id;
  using message = std::pair<player_id, std::string>;

  test_game(const json& data): m_done(false) {
    try {
      if(data.at("matched") == true) {
        m_valid = true;
      } else {
        m_valid = false;
      }
    } catch(std::exception& e) {
      m_valid = false;
    }
  }
  
  void connect(vector<message>& out_msg_list, player_id id) {
    m_player_list.insert(id);
  }

  void disconnect(vector<message>& out_msg_list, player_id id) {
    m_player_list.erase(id);
  }

  void update(
      vector<message>& out_msg_list,
      const vector<message>& in_msg_list,
      long delta_time
    )
  {
    for(const message& msg : in_msg_list) {
      try {
        json msg_json = json::parse(msg.second);
        if(msg_json.at("type") == "broadcast") {
          for(player_id pid : m_player_list) {
            json temp = {
                { "pid", msg.first }, { "data", msg_json.at("data") }
              };
            out_msg_list.emplace_back(pid, temp.dump());
          }
        } else if(msg_json.at("type") == "echo") {
          out_msg_list.emplace_back(msg.first, msg.second);
        } else if(msg_json.at("type") == "stop") {
          m_done = true;
        } else {
          spdlog::error("client sent message without type: {}", msg.second);
        }
      }
      catch(std::exception& e) {
        spdlog::error(
            "error parsing message \"{}\": {}",
            msg.second,
            e.what()
          );
      }
    }
  }

  bool is_done() const {
    return m_done;
  }

  bool is_valid() const {
    return m_valid;
  }

  json get_state() const {
    json data;
    data["valid"] = m_valid;
    data["done"] = m_done;
    return data;
  }

private:
  unordered_set<player_id> m_player_list;
  bool m_done, m_valid;
};

class test_matchmaker {
public:
  using player_traits = test_player_traits;
  using session_id = player_traits::id::session_id;
  using id_hash = player_traits::id::hash;
  using message = std::pair<session_id, std::string>;
  using game = std::tuple<std::vector<session_id>, session_id, json>;

  struct session_data {
    session_data(const json& data) {}

    bool is_valid() {
      return true;
    }
  };

  test_matchmaker() : m_sid_count(0) {}

  bool can_match(
      const unordered_map<session_id, session_data, id_hash>& session_map
    )
  {
    return session_map.size() > 1;
  }

  void match(
      vector<game>& game_list,
      vector<message>& messages,
      const unordered_map<session_id, session_data, id_hash>& session_map,
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
