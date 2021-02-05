#ifndef JWT_GAME_SERVER_GAME_SERVER_HPP
#define JWT_GAME_SERVER_GAME_SERVER_HPP

#include "base_server.hpp"

#include <chrono>
#include <algorithm>
#include <execution>

namespace jwt_game_server {
  // time literals to initialize timestep variables
  using namespace std::chrono_literals;

  template<typename game_instance, typename jwt_clock, typename json_traits,
    typename server_config, typename close_reasons = default_close_reasons>
  class game_server : public base_server<typename game_instance::player_traits,
      jwt_clock, json_traits, server_config, close_reasons>
  {
  // type definitions
  private:
    using super = base_server<typename game_instance::player_traits, jwt_clock,
      json_traits, server_config, close_reasons>;

    using combined_id = typename super::combined_id;
    using player_id = typename super::player_id;
    using session_id = typename super::session_id;
    using id_hash = typename super::id_hash;

    using message = typename game_instance::message;

    using json = typename super::json;
    using clock = typename super::clock;

  // main class body
  public:
    game_server(
        const jwt::verifier<jwt_clock, json_traits>& v,
        function<std::string(combined_id, const json&)> f
      ) : super(v, f, 3600s) {}

    game_server(
        const jwt::verifier<jwt_clock, json_traits>& v,
        function<std::string(combined_id, const json&)> f,
        std::chrono::milliseconds t
      ) : super(v, f, t) {}

    void stop() {
      super::stop();

      lock_guard<mutex> guard(m_game_list_lock);
      m_games.clear();
      m_messages.clear();
    }

    void update_games(std::chrono::milliseconds timestep) {
      auto time_start = clock::now();
      while(super::is_running()) {
        const auto delta_time =
          std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now() - time_start
          );

        if(delta_time >= timestep) {
          unique_lock<mutex> game_lock(m_game_list_lock);

          // ensure that each game has a vector to save messages to
          for(auto it = m_games.begin(); it != m_games.end(); it++) {
            m_messages.try_emplace(it->first, vector<message>{});
          }

          time_start = clock::now();
          // game updates are completely independent, so exec in parallel
          std::for_each(
              std::execution::par,
              m_games.begin(),
              m_games.end(),
              [=](auto& key_val_pair){
                key_val_pair.second.game_update(
                    m_messages[key_val_pair.first],
                    delta_time.count()
                  );
              }
            );

          game_lock.unlock();

          // base_server actions are thread locked so send messages in sequence
          for(auto it = m_messages.begin(); it != m_messages.end(); it++) {
            send_messages(it->first, it->second);
            it->second.clear();
          }

          std::vector<std::pair<session_id, json> > complete_games;
          
          game_lock.lock();

          // erase finished games
          for(auto it = m_games.begin(); it != m_games.end();) {
            if(it->second.is_done()) {
              spdlog::debug("game session {} ended", it->first);
              m_messages.erase(it->first);
              complete_games.emplace_back(it->first, it->second.get_state());
              it = m_games.erase(it);
            } else {
              it++;
            }
          }

          game_lock.unlock();

          // complete sessions and send result tokens out for finished games
          for(const auto& sid_json_pair : complete_games) { 
            super::complete_session(
                sid_json_pair.first,
                sid_json_pair.first,
                sid_json_pair.second
              );
          }
        }

        std::this_thread::sleep_for(std::min(1ms, timestep-delta_time));
      }
    }

    std::size_t get_game_count() {
      lock_guard<mutex> guard(m_game_list_lock);
      return m_games.size();
    }

  private:
    // send given list of game messages to players
    void send_messages(
        const session_id& sid,
        const vector<message>& message_list
      )
    {
      for(const message& msg : message_list) {
        super::send_message(
            { msg.id, sid },
            msg.text
          );
      }
    }

    void process_message(const combined_id& id, const json& data) {
      super::process_message(id, data);

      unique_lock<mutex> lock(m_game_list_lock);
      auto it = m_games.find(id.session);
      if(it != m_games.end()) {
        vector<message> msg_list;
        it->second.player_update(msg_list, id.player, data);
        lock.unlock();
        send_messages(id.session, msg_list);
      }
    }

    void player_connect(const combined_id& id, const json& data) {
      super::player_connect(id, data);

      unique_lock<mutex> lock(m_game_list_lock);
      auto it = m_games.find(id.session);
      if(it == m_games.end()) {
        game_instance game{data};

        if(!game.is_valid()) {
          spdlog::error("connection provided invalid game data");
          super::complete_session(id.session, id.session, game.get_state());
          return;
        }

        it = m_games.emplace(id.session, std::move(game)).first;
      }
 
      vector<message> msg_list;
      it->second.connect(msg_list, id.player);

      lock.unlock();

      send_messages(id.session, msg_list);
    }

    void player_disconnect(const combined_id& id) {
      super::player_disconnect(id);
      {
        unique_lock<mutex> game_lock(m_game_list_lock);
        auto it = m_games.find(id.session);
        if(it != m_games.end()) {
          vector<message> msg_list;
          it->second.disconnect(msg_list, id.player);

          game_lock.unlock();

          send_messages(id.session, msg_list);
        }
      }
    }

    // member variables
    unordered_map<
        session_id,
        game_instance,
        id_hash
      > m_games;

    unordered_map<
        session_id,
        vector<message>,
        id_hash
      > m_messages;

    // m_game_list_lock guards the member m_games
    mutex m_game_list_lock;
  };
}

#endif // JWT_GAME_SERVER_GAME_SERVER_HPP
