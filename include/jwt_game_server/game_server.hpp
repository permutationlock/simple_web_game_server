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
  class game_server {
  // type definitions
  private:
    using jwt_base_server = base_server<
        typename game_instance::player_traits,
        jwt_clock,
        json_traits,
        server_config,
        close_reasons
      >;

    using combined_id = typename jwt_base_server::combined_id;
    using player_id = typename jwt_base_server::player_id;
    using session_id = typename jwt_base_server::session_id;
    using id_hash = typename jwt_base_server::id_hash;

    using message = pair<player_id, std::string>;

    using json = typename jwt_base_server::json;
    using clock = typename jwt_base_server::clock;

    struct connection_update {
      connection_update(const combined_id& i) : id(i), disconnection(true) {}
      connection_update(const combined_id& i, const json& d) : id(i),
        data(d), disconnection(false) {}

      combined_id id;
      json data;
      bool disconnection;
    };

  // main class body
  public:
    game_server(
        const jwt::verifier<jwt_clock, json_traits>& v,
        function<std::string(const combined_id&, const json&)> f,
        std::chrono::milliseconds t
      ) : m_jwt_server(v, f, t)
    {
      m_jwt_server.set_open_handler(
          bind(
            &game_server::player_connect,
            this,
            jwt_game_server::_1,
            jwt_game_server::_2
          )
        );
      m_jwt_server.set_close_handler(
          bind(
            &game_server::player_disconnect,
            this,
            jwt_game_server::_1
          )
        );
      m_jwt_server.set_message_handler(
          bind(
            &game_server::process_message,
            this,
            jwt_game_server::_1,
            jwt_game_server::_2
          )
        );
    }

    game_server(
        const jwt::verifier<jwt_clock, json_traits>& v,
        function<std::string(const combined_id&, const json&)> f
      ) : game_server(v, f, 3600s) {}

    void run(uint16_t port, bool unlock_address = false) {
      m_jwt_server.run(port, unlock_address);
    }

    void process_messages() {
      m_jwt_server.process_messages();
    }

    void reset() {
      stop();
      m_jwt_server.reset();
    }

    void stop() {
      m_jwt_server.stop();

      {
        lock_guard<mutex> guard(m_game_list_lock);
        m_games.clear();
        m_out_messages.clear();
      }
      {
        lock_guard<mutex> guard(m_in_message_list_lock);
        m_in_messages.clear();
      }
      {
        lock_guard<mutex> guard(m_connection_update_list_lock);
        m_connection_updates.clear();
      }
    }

    std::size_t get_player_count() {
      return m_jwt_server.get_player_count();
    }

    bool is_running() {
      return m_jwt_server.is_running();
    }

    void update_games(std::chrono::milliseconds timestep) {
      auto time_start = clock::now();
      vector<session_id> finished_games;

      while(m_jwt_server.is_running()) {
        const auto delta_time =
          std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now() - time_start
          );

        unique_lock<mutex> game_lock(m_game_list_lock);
        if(m_games.empty()) {
          game_lock.unlock();
          unique_lock<mutex> conn_lock(m_connection_update_list_lock);
          while(m_connection_updates.empty()) {
            m_game_condition.wait(conn_lock);
            if(!m_jwt_server.is_running()) {
              return;
            }
          }
          game_lock.lock();
        }

        if(delta_time < timestep) {
          game_lock.unlock();
          std::this_thread::sleep_for(std::min(1ms, timestep-delta_time));
        } else {
          time_start = clock::now();
          process_connection_updates();

          // we remove game data here to catch any possible players submitting
          // new connections in the last timestep when the game session ends
          for(session_id sid : finished_games) {
            spdlog::trace("erasing game session {}", sid);
            m_out_messages.erase(sid);
            m_games.erase(sid);
          }
          finished_games.clear();

          process_game_updates(delta_time.count());

          for(auto it = m_out_messages.begin(); it != m_out_messages.end();
              ++it)
          {
            for(const message& msg : it->second) {
              m_jwt_server.send_message(
                  { msg.first, it->first },
                  msg.second
                );
            }
            it->second.clear();
          }

          for(auto it = m_games.begin(); it != m_games.end(); ++it) {
            if(it->second.is_done()) {
              spdlog::debug("game session {} ended", it->first);
              m_jwt_server.complete_session(
                  it->first,
                  it->first,
                  it->second.get_state()
                );
              finished_games.push_back(it->first);
            }
          }
        }
      }
    }

    std::size_t get_game_count() {
      lock_guard<mutex> guard(m_game_list_lock);
      return m_games.size();
    }

  private:
    void process_connection_updates() {
      vector<connection_update> connection_updates;
      {
        lock_guard<mutex> conn_guard(m_connection_update_list_lock);
        std::swap(connection_updates, m_connection_updates);
      }

      for(connection_update& update : connection_updates) {
        auto it = m_games.find(update.id.session);
        if(update.disconnection) {
          if(it != m_games.end()) {
            it->second.disconnect(update.id.player);
          }
        } else {
          if(it == m_games.end()) {
            game_instance game{update.data};

            if(!game.is_valid()) {
              spdlog::error("connection provided invalid game data");
              m_jwt_server.complete_session(
                  update.id.session, update.id.session, game.get_state()
                );
              continue;
            }

            spdlog::error("creating game session {}", update.id.session);
            it = m_games.emplace(
                update.id.session, std::move(game)
              ).first;
            m_out_messages.emplace(
                update.id.session, vector<message>{}
              );
          }
     
          it->second.connect(update.id.player);
        }
      }
    }

    void process_game_updates(long delta_time) {
      unordered_map<session_id, vector<message>, id_hash> in_messages;
      in_messages.reserve(m_games.size());
      {
        lock_guard<mutex> msg_guard(m_in_message_list_lock);
        std::swap(in_messages, m_in_messages);
      }

      // game updates are completely independent, so exec in parallel
      std::for_each(
          std::execution::par,
          m_games.begin(),
          m_games.end(),
          [&](auto& key_val_pair){
            auto in_msg_it = in_messages.find(key_val_pair.first);
            if(in_msg_it != in_messages.end()) {
              key_val_pair.second.update(
                  m_out_messages.at(key_val_pair.first),
                  in_msg_it->second,
                  delta_time
                );
            } else {
              key_val_pair.second.update(
                  m_out_messages.at(key_val_pair.first),
                  vector<message>{},
                  delta_time
                );
            }
          }
        );
    }

    void process_message(const combined_id& id, std::string&& data) {
      lock_guard<mutex> msg_guard(m_in_message_list_lock);
      m_in_messages[id.session].emplace_back(
          id.player, std::forward<std::string>(data)
        );
    }

    void player_connect(const combined_id& id, const json& data) {
      {
        lock_guard<mutex> guard(m_connection_update_list_lock);
        m_connection_updates.emplace_back(id, data);
      }
      m_game_condition.notify_one();
    }

    void player_disconnect(const combined_id& id) {
      lock_guard<mutex> guard(m_connection_update_list_lock);
      m_connection_updates.emplace_back(id);
    }

    // member variables
    unordered_map<
        session_id,
        game_instance,
        id_hash
      > m_games;
    mutex m_game_list_lock;

    unordered_map<
        session_id,
        vector<message>,
        id_hash
      > m_in_messages;
    mutex m_in_message_list_lock;

    std::vector<connection_update> m_connection_updates;
    mutex m_connection_update_list_lock;

    condition_variable m_game_condition;

    unordered_map<session_id, vector<message>, id_hash> m_out_messages;

    jwt_base_server m_jwt_server;
  };
}

#endif // JWT_GAME_SERVER_GAME_SERVER_HPP
