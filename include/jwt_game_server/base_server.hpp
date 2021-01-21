#ifndef JWT_GAME_SERVER_BASE_SERVER_HPP
#define JWT_GAME_SERVER_BASE_SERVER_HPP

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <jwt-cpp/jwt.h>

#include <spdlog/spdlog.h>

#include <vector>
#include <queue>
#include <set>
#include <map>

#include <utility>

#include <functional>

#include <atomic>
#include <mutex>
#include <condition_variable>

namespace jwt_game_server {
  // websocketpp types
  using websocketpp::connection_hdl;

  // datatype implementations
  using std::vector;
  using std::pair;
  using std::set;
  using std::map;
  using std::queue;

  // functional types
  using std::function;
  using std::bind;
  using std::placeholders::_1;
  using std::placeholders::_2;

  // threading type implementations
  using std::atomic;
  using std::mutex;
  using std::lock_guard;
  using std::unique_lock;
  using std::condition_variable;

  template<typename player_traits, typename jwt_clock, typename json_traits,
    typename server_config>
  class base_server {
  // type definitions
  protected:
    using combined_id = typename player_traits::id;
    using player_id = typename combined_id::player_id;
    using session_id = typename combined_id::session_id;
    using json = typename json_traits::json;
    using clock = std::chrono::high_resolution_clock;
    using time_point = std::chrono::time_point<clock>;

  private:
    enum action_type {
      SUBSCRIBE,
      UNSUBSCRIBE,
      IN_MESSAGE,
      OUT_MESSAGE,
      CLOSE_CONNECTION
    };

    using ws_server = websocketpp::server<server_config>;
    using message_ptr = typename ws_server::message_ptr;

    struct action {
      action(action_type t, connection_hdl h) : type(t), hdl(h) {}
      action(action_type t, connection_hdl h, const std::string& m)
        : type(t), hdl(h), msg(m) {}

      action_type type;
      connection_hdl hdl;
      std::string msg;
    };

    template<typename map_t>
    class buffered_map {
    public:
      using key_type = typename map_t::key_type;
      using mapped_type = typename map_t::mapped_type;

      void insert(const pair<key_type, mapped_type>& p) {
        m_map1.insert(p);
      }

      void insert(pair<key_type, mapped_type>&& p) {
        m_map1.insert(p);
      }

      void refresh(const key_type& key) {
        if(contains(key)) {
          m_map1.insert(std::make_pair(key, this->at(key)));
        }
      }

      mapped_type& at(const key_type& key) {
        if(m_map1.count(key) > 0) {
          return m_map1.find(key)->second;
        } else if(m_map2.count(key) > 0) {
          return m_map2.find(key)->second;
        } else {
          throw std::out_of_range{"key not in buffered_map"};
        }
      }

      bool contains(const key_type& key) {
        bool result = false;
        if(m_map1.count(key) > 0) {
          result = true;
        } else if(m_map2.count(key) > 0) {
          result = true;
        }
        return result;
      }

      void erase(const key_type& key) {
        m_map1.erase(key);
        m_map2.erase(key);
      }

      void clear() {
        m_map2.clear();
        std::swap(m_map1, m_map2);
      }

      map_t m_map1;
      map_t m_map2;
    };

  // main class body
  public:
    base_server(
        const jwt::verifier<jwt_clock, json_traits>& v,
        function<std::string(const combined_id&, const json&)> f,
        std::chrono::milliseconds t
      ) : m_is_running(false), m_jwt_verifier(v), m_get_result_str(f),
          m_session_release_time(t)
    {
      m_server.init_asio();

      m_server.set_open_handler(bind(&base_server::on_open, this,
        jwt_game_server::_1));
      m_server.set_close_handler(bind(&base_server::on_close, this,
        jwt_game_server::_1));
      m_server.set_message_handler(
          bind(&base_server::on_message, this, jwt_game_server::_1,
            jwt_game_server::_2)
        );
    }

    void run(uint16_t port, bool unlock_address = false) {      
      if(!m_is_running) {
        spdlog::info("server is listening on port {}", port);
        m_is_running = true;

        {
          lock_guard<mutex> action_guard(m_action_lock);
          while(!m_actions.empty()) {
            m_actions.pop();
          }
        }

        try {
          m_server.set_reuse_addr(unlock_address);
          m_server.listen(port);
          m_server.start_accept();

          m_server.run();
        } catch (std::exception& e) {
          m_is_running = false;
          spdlog::error("error running server: {}", e.what());
        }
      } else {
        spdlog::info("server is already running");
      }
    }

    bool is_running() {
      return m_is_running;
    }

    void reset() {
      if(m_is_running) {
        this->stop();
      }
      m_server.reset();
    }

    // stop may be overloaded for subclasses to halt worker threads
    virtual void stop() {
      m_is_running = false;
      m_server.stop_listening();
      {
        lock_guard<mutex> action_guard(m_action_lock);
        lock_guard<mutex> conn_guard(m_connection_lock);

        // collect all unresolved connection actions
        while(!m_actions.empty()) {
          action a = m_actions.front();
          m_actions.pop();
          if(a.type == SUBSCRIBE || a.type == CLOSE_CONNECTION) {
            m_new_connections.insert(a.hdl);
          }
        }

        // collect all open player connections
        for(auto& con_pair : m_connection_ids) {
          connection_hdl hdl = con_pair.first;
          m_new_connections.insert(hdl);
        }

        // close all remaining open connections
        for(connection_hdl hdl : m_new_connections) {
          try {
            m_server.close(
                hdl,
                websocketpp::close::status::normal,
                "server shutdown"
              );
          } catch (std::exception& e) {
            spdlog::debug("error closing connection: {}", e.what());
          }
        }
      }
      m_connection_ids.clear();
      m_id_connections.clear();
      m_new_connections.clear();
      m_action_cond.notify_all();
    }

    void process_messages() {
      while(m_is_running) {
        unique_lock<mutex> action_lock(m_action_lock);

        while(m_actions.empty()) {
          m_action_cond.wait(action_lock);
          if(!m_is_running) {
            return;
          }
        }

        action a = m_actions.front();
        m_actions.pop();

        action_lock.unlock();

        if (a.type == SUBSCRIBE) {
          spdlog::trace("processing SUBSCRIBE action");
          unique_lock<mutex> conn_lock(m_connection_lock);
          m_new_connections.insert(a.hdl);
        } else if (a.type == UNSUBSCRIBE) {
          spdlog::trace("processing UNSUBSCRIBE action");
          unique_lock<mutex> conn_lock(m_connection_lock);
          if(m_connection_ids.count(a.hdl) < 1) {
            spdlog::trace("player disconnected without providing id");
          } else {
            // connection provided a player id
            conn_lock.unlock();
            this->player_disconnect(m_connection_ids[a.hdl]);
          }
        } else if (a.type == IN_MESSAGE) {
          spdlog::trace("processing IN_MESSAGE action");
          unique_lock<mutex> conn_lock(m_connection_lock);

          if(m_connection_ids.count(a.hdl) < 1) {
            spdlog::trace("recieved message from connection w/no id");
            conn_lock.unlock();

            try {
              this->open_session(a.hdl, a.msg);
            } catch(std::exception& e) {
              spdlog::debug("error setting up id: {}", e.what());
            }
          } else {
            combined_id id = m_connection_ids[a.hdl];
            conn_lock.unlock();

            spdlog::trace("processing message from player {} with session {}", id.player, id.session);
            json msg_json;
            try {
              json_traits::parse(msg_json, a.msg);
            } catch(std::exception& e) {
              spdlog::debug(
                  "update from player {} with session {} was not valid json",
                  id.player, id.session
                );
              return;
            }
            try {
              this->process_message(id, msg_json);
            } catch(std::exception& e) {
              spdlog::debug("error processing message: {}", e.what());
            }
          }
        } else if(a.type == OUT_MESSAGE) {
          spdlog::trace("processing OUT_MESSAGE action");
          try {
            m_server.send(a.hdl, a.msg, websocketpp::frame::opcode::text);
          } catch (std::exception& e) {
            spdlog::debug(
                "error sending message \"{}\": {}",
                a.msg,
                e.what()
              );
          }
        } else if(a.type == CLOSE_CONNECTION) { 
          try {
            m_server.close(
                a.hdl,
                websocketpp::close::status::normal,
                a.msg
              );
          } catch (std::exception& e) {
            spdlog::debug("error closing connection: {}", e.what());
          }
        } else {
          // undefined.
        }
      }
    }

    std::size_t get_player_count() {
      lock_guard<mutex> guard(m_connection_lock);
      return m_connection_ids.size();
    }

  protected:
    void send_message(const combined_id& id, const std::string& msg) {
      connection_hdl hdl;
      if(get_connection_hdl_from_id(id, hdl)) {
        send_to_hdl(hdl, msg);
      } else {
        spdlog::trace(
            "ignored message sent to player {} with session {}: connection closed",
            id.player, id.session
          );
      }
    }

    void close_connection(const combined_id& id, const std::string& reason) {
      connection_hdl hdl;
      if(get_connection_hdl_from_id(id, hdl)) {
        close_hdl(hdl, reason);
      } else {
        spdlog::trace(
            "can't close player {} session {}: connection already closed",
            id.player,
            id.session
          );
      }
    }

    void complete_connection(
        const combined_id& id,
        const combined_id& result_id,
        const json& data
      )
    {
      std::string result_token = m_get_result_str(result_id, data);
      {
        lock_guard<mutex> guard(m_session_lock);
        update_session_locks();
        close_session(id, result_token);
      }
      send_message(id, result_token);
      close_connection(id, "session completed");
    }

    // if a subclass chooses to overload one of the following three virtual
    // functions, it must call the base class version

    virtual void process_message(const combined_id& id, const json& data) {
      spdlog::debug("player {} with session {} sent: {}",
          id.player, id.session, data.dump());
    }

    virtual bool player_connect(const combined_id& id, const json& data) {
      spdlog::debug(
          "player {} connected with session {}: {}",
          id.player,
          id.session,
          data.dump()
        );

      return true;
    }

    virtual void player_disconnect(const combined_id& id) {
      connection_hdl hdl;
      if(get_connection_hdl_from_id(id, hdl)) {
        lock_guard<mutex> connection_guard(m_connection_lock);
        m_connection_ids.erase(hdl);
        m_id_connections.erase(id);
      }

      spdlog::debug("player {} with session {} disconnected",
          id.player, id.session);
    }

  private:
    bool get_connection_hdl_from_id(const combined_id& id, connection_hdl& hdl) {
      lock_guard<mutex> guard(m_connection_lock);
      if(m_id_connections.count(id) > 0) {
        hdl = m_id_connections[id];
        return true;
      }

      return false;
    }

    void send_to_hdl(connection_hdl hdl, const std::string& msg) {
      {
        lock_guard<mutex> guard(m_action_lock);
        spdlog::trace("out_message: {}", msg);
        m_actions.push(action(OUT_MESSAGE, hdl, msg));
      }
      m_action_cond.notify_one();
    }

    void close_hdl(connection_hdl hdl, const std::string& reason) {
      {
        lock_guard<mutex> guard(m_action_lock);
        spdlog::trace("close_connection: {}", reason);
        m_actions.push(action(UNSUBSCRIBE,hdl));
        m_actions.push(action(CLOSE_CONNECTION, hdl, reason));
      }
      m_action_cond.notify_one();
    }

    void on_open(connection_hdl hdl) {
      {
        lock_guard<mutex> guard(m_action_lock);
        if(m_is_running) {
          spdlog::trace("connection opened");
          m_actions.push(action(SUBSCRIBE,hdl));
        } else {
          try {
            m_server.close(
                hdl,
                websocketpp::close::status::normal,
                "server closed"
              );
          } catch (std::exception& e) {
            spdlog::debug("error closing connection: {}",
                e.what());
          }
        }
      }
      m_action_cond.notify_one();
    }

    void on_close(connection_hdl hdl) {
      {
        lock_guard<mutex> guard(m_connection_lock);
        if(m_connection_ids.count(hdl) == 0) {
          return;
        }
      }
      {
        lock_guard<mutex> guard(m_action_lock);
        spdlog::trace("connection closed");
        m_actions.push(action(UNSUBSCRIBE,hdl));
      }
      m_action_cond.notify_one();
    }

    void on_message(connection_hdl hdl, message_ptr msg) {
      {
        lock_guard<mutex> guard(m_action_lock);
        spdlog::trace("in message: {}", msg->get_payload());
        m_actions.push(action(IN_MESSAGE, hdl, msg->get_payload()));
      }
      m_action_cond.notify_one();
    }

    void update_session_locks() {
      auto delta_time = std::chrono::duration_cast<std::chrono::milliseconds>(
          clock::now() - m_last_session_update_time
        );

      // free up sessions if enough time has passed
      if(delta_time > m_session_release_time) {
        if(delta_time > 2 * m_session_release_time) {
          m_locked_sessions.clear();
        }
        m_locked_sessions.clear();
        m_last_session_update_time = clock::now();
      }
    }

    void close_session(const combined_id& id, const std::string& result_token) {
      m_locked_sessions.insert(std::make_pair(id, result_token));
    }

    void open_session(connection_hdl hdl, const std::string& login_token) {
      player_id pid;
      session_id sid;
      json login_json;
      bool completed = false;
      try {
        jwt::decoded_jwt<json_traits> decoded_token =
          jwt::decode<json_traits>(login_token);
        auto claim_map = decoded_token.get_payload_claims();
        pid = player_traits::parse_player_id(claim_map.at("pid").to_json());
        sid = player_traits::parse_session_id(claim_map.at("sid").to_json());
        login_json = claim_map.at("data").to_json();
        m_jwt_verifier.verify(decoded_token);
        completed = true;
      } catch(std::out_of_range& e) {
        spdlog::debug("connection provided jwt without id and/or data claim: {}", e.what());
      } catch(jwt::error::token_verification_exception& e) {
        spdlog::debug("connection provided jwt that could not be verified: {}", e.what());
      } catch(std::invalid_argument& e) { 
        spdlog::debug("connection provided invalid jwt token string: {}", e.what());
      } catch(std::runtime_error& e) {
        spdlog::debug("connection provided invalid json in jwt: {}", e.what());
      } catch(std::exception& e) {
        spdlog::debug("error verifying player: {}", e.what());
      }

      combined_id id{pid, sid};

      if(completed) {
        lock_guard<mutex> guard(m_session_lock);
        update_session_locks();
        
        if(!m_locked_sessions.contains(id)) {
          {
            lock_guard<mutex> guard(m_connection_lock);

            // immediately close duplicate connections to avoid complications
            if(m_id_connections.count(id) > 0) {
              spdlog::debug(
                  "closing duplicate connection for player {} session {}",
                  id.player,
                  id.session
                );

              try {
                m_server.close(
                    m_id_connections[id],
                    websocketpp::close::status::normal,
                    "duplicate connection"
                  );
              } catch (std::exception& e) {
                spdlog::debug("error closing duplicate connection: {}",
                  e.what());
              }

              m_connection_ids.erase(m_id_connections[id]);
            }

            m_connection_ids[hdl] = id;
            m_id_connections[id] = hdl;
            m_new_connections.erase(hdl);
          }

          if(!this->player_connect(id, login_json)) {
            close_session(id, m_get_result_str(id, json{json::value_t::null}));
            close_hdl(hdl, "invalid data claim");
          }
        } else {
          send_to_hdl(hdl, m_locked_sessions.at(id));
          close_hdl(hdl, "session locked");
        }
      } else {
        close_hdl(hdl, "failed jwt verification");
      }
    }

    // member variables
    ws_server m_server;
    atomic<bool> m_is_running;

    jwt::verifier<jwt_clock, json_traits> m_jwt_verifier;
    function<std::string(const combined_id&, const json&)> m_get_result_str;

    set<connection_hdl, std::owner_less<connection_hdl> > m_new_connections;
    map<
        connection_hdl,
        combined_id,
        std::owner_less<connection_hdl>
      > m_connection_ids;
    map<combined_id, connection_hdl> m_id_connections;

    // m_connection_lock guards the members m_new_connections,
    // m_id_connections, and m_connection_ids
    mutex m_connection_lock;

    time_point m_last_session_update_time;
    std::chrono::milliseconds m_session_release_time;

    buffered_map<
        map<combined_id, std::string>
      > m_locked_sessions;

    // m_action_lock guards the members m_locked_sessions,
    // m_locked_sessions_buffer, and m_session_ids
    mutex m_session_lock;

    queue<action> m_actions;

    // m_action_lock guards the member m_actions
    mutex m_action_lock;
    condition_variable m_action_cond;
  };
}

#endif // JWT_GAME_SERVER_BASE_SERVER_HPP