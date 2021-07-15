#ifndef CHAT_GAME_HPP
#define CHAT_GAME_HPP

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <vector>
#include <unordered_set>

using std::vector;
using std::unordered_set;

struct chat_player_traits {
  struct id {
    using player_id = std::string;
    using session_id = std::string;

    struct hash {
      std::size_t operator()(const id& id_data) const {
        return std::hash<player_id>{}(id_data.player)
          ^ std::hash<session_id>{}(id_data.session);
      }

      std::size_t operator()(const std::string& str_id) const {
        return std::hash<std::string>{}(str_id);
      }
    };

    template<typename value>
    using map = std::unordered_map<id, value, hash>;

    template<typename value>
    using session_id_map = std::unordered_map<session_id, value, hash>;

    id() {}
    id(const player_id& p, const session_id& s) : player(p), session(s) {}

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

class chat_game {
public:
  using player_traits = chat_player_traits;
  using player_id = player_traits::id::player_id;
  using message = std::pair<player_id, std::string>;

  chat_game(const json& msg) {}
  
  void connect(vector<message>& out_messages, const player_id& pid) {
    for(const player_id& player: m_player_list) {
      out_messages.emplace_back(
          player,
          std::string(pid) + std::string(" connected")
        );
    }
    m_player_list.insert(pid);
  }

  void disconnect(vector<message>& out_messages, const player_id& pid) {
    m_player_list.erase(pid);

    for(const player_id& player: m_player_list) {
      out_messages.emplace_back(
          player,
          std::string(pid) + std::string(" disconnected")
        );
    }
  }

  void update(
      vector<message>& out_messages,
      const vector<message>& in_messages,
      long delta_time
    )
  {
    for(const message& msg : in_messages) {
      for(const player_id& player : m_player_list) {
        out_messages.emplace_back(
            player,
            msg.first + std::string(": ") + msg.second
          );
      }
    }
  }

  json get_state() const {
    json game_json{json::value_t::object};
 
    return game_json;
  }

  bool is_done() const {
    return m_player_list.empty();
  }
  
  bool is_valid() const {
    return true;
  }

private:
  unordered_set<player_id> m_player_list;
};

#endif // CHAT_GAME_HPP
