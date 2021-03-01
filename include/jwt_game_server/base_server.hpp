#ifndef JWT_GAME_SERVER_BASE_SERVER_HPP
#define JWT_GAME_SERVER_BASE_SERVER_HPP

#include <websocketpp/server.hpp>
#include <websocketpp/common/asio_ssl.hpp>
#include <websocketpp/common/asio.hpp>

#include <jwt-cpp/jwt.h>

#include <spdlog/spdlog.h>

#include <vector>
#include <queue>
#include <set>
#include <map>
#include <unordered_map>

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
  using std::unordered_map;
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

  struct default_close_reasons {
    static inline std::string invalid_jwt() {
      return "INVALID_TOKEN";
    };
    static inline std::string duplicate_connection() {
      return "DUPLICATE_CONNECTION";
    };
    static inline std::string server_shutdown() {
      return "SERVER_SHUTDOWN";
    };
    static inline std::string session_complete() {
      return "SESSION_COMPLETE";
    };
  };

  template<typename player_traits, typename jwt_clock, typename json_traits,
    typename server_config, typename close_reasons>
  class base_server {
  // type definitions
  public: 
    class server_error : public std::runtime_error {
    public:
      using super = std::runtime_error;
      explicit server_error(const std::string& what_arg) noexcept :
        super(what_arg) {}
      explicit server_error(const char* what_arg) noexcept : super(what_arg) {}
    };

    using combined_id = typename player_traits::id;
    using player_id = typename combined_id::player_id;
    using session_id = typename combined_id::session_id;
    using id_hash = typename combined_id::hash;

    using json = typename json_traits::json;
    using clock = std::chrono::high_resolution_clock;

    using ssl_context_ptr = 
      websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context>;

  private:
    using time_point = std::chrono::time_point<clock>;

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
      action(action_type t, connection_hdl h, std::string&& m)
        : type(t), hdl(h), msg(std::move(m)) {}
      action(action_type t, connection_hdl h, const std::string& m)
        : type(t), hdl(h), msg(m) {}

      action_type type;
      connection_hdl hdl;
      std::string msg;
    };

    struct session_data {
      session_data(const session_id& s, json&& d) : session(s), data(d) {}
      session_id session;
      json data;
    };

    template<typename map_type>
    class buffered_map {
    public:
      using key_type = typename map_type::key_type;
      using mapped_type = typename map_type::mapped_type;
      using value_type = typename map_type::value_type;

      void insert(value_type&& p) {
        m_map1.insert(std::move(p));
      }

      const mapped_type& at(const key_type& key) const {
        auto it = m_map1.find(key);
        if(it != m_map1.end()) {
          return it->second;
        } else {
          it = m_map2.find(key);
          if(it != m_map2.end()) {
            return it->second;
          } else {
            throw std::out_of_range{"key not in buffered_map"};
          }
        }
      }

      bool contains(const key_type& key) const {
        bool result = false;
        if(m_map1.count(key) > 0) {
          result = true;
        } else if(m_map2.count(key) > 0) {
          result = true;
        }
        return result;
      }

      void clear() {
        m_map2.clear();
        std::swap(m_map1, m_map2);
      }

      map_type m_map1;
      map_type m_map2;
    };

  // main class body
  public:
    base_server(
        const jwt::verifier<jwt_clock, json_traits>& v,
        function<std::string(const combined_id&, const json&)> f,
        std::chrono::milliseconds t
      ) : m_is_running(false), m_jwt_verifier(v), m_get_result_str(f),
          m_session_release_time(t),
          m_handle_open([](const combined_id&, json&&){}),
          m_handle_close([](const combined_id&){}),
          m_handle_message([](const combined_id&, std::string&&){})
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

    void set_tls_init_handler(function<ssl_context_ptr(connection_hdl)> f) {
      if(!m_is_running) {
        m_server.set_tls_init_handler(f);
      } else {
        throw server_error{"set_tls_init_handler called on running server"};
      }
    }

    void set_open_handler(function<void(const combined_id&,json&&)> f) {
      if(!m_is_running) {
        m_handle_open = f;
      } else {
        throw server_error{"set_open_handler called on running server"};
      }
    }

    void set_close_handler(function<void(const combined_id&)> f) {
      if(!m_is_running) {
        m_handle_close = f;
      } else {
        throw server_error{"set_close_handler called on running server"};
      }
    }

    void set_message_handler(function<void(const combined_id&,std::string&&)> f) {
      if(!m_is_running) {
        m_handle_message = f;
      } else {
        throw server_error{"set_message_handler called on running server"};
      }
    }

    void run(uint16_t port, bool unlock_address) {
      if(!m_is_running) {
        spdlog::info("server is listening on port {}", port);
        m_is_running = true;

        {
          lock_guard<mutex> action_guard(m_action_lock);
          while(!m_actions.empty()) {
            m_actions.pop();
          }
        }

        m_server.set_reuse_addr(unlock_address);
        m_server.listen(port);
        m_server.start_accept();

        m_server.run();
      } else {
        throw server_error("run called on running server");
      }
    }

    bool is_running() {
      return m_is_running;
    }

    void reset() {
      if(m_is_running) {
        stop();
      }
      m_server.reset();
    }

    void stop() {
      if(m_is_running) {
        m_is_running = false;
        m_server.stop_listening();
        {
          lock_guard<mutex> action_guard(m_action_lock);
          lock_guard<mutex> session_guard(m_session_lock);
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
            m_new_connections.insert(con_pair.first);
          }

          // close all remaining open connections
          for(connection_hdl hdl : m_new_connections) {
            close_hdl(hdl, close_reasons::server_shutdown());
          }

          m_connection_ids.clear();
          m_id_connections.clear();
          m_new_connections.clear();
          m_locked_sessions.clear();
          m_locked_sessions.clear();
          m_session_players.clear();
        }
        m_action_cond.notify_all();
      } else {
        throw server_error("stop called on stopped server");
      }
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

          auto it = m_connection_ids.find(a.hdl);
          if(it == m_connection_ids.end()) {
            m_new_connections.erase(a.hdl);
            spdlog::trace(
                "client hdl {} disconnected without opening session",
                a.hdl.lock().get()
              );
          } else {
            // connection provided a player id
            combined_id id = it->second;
            conn_lock.unlock();
            player_disconnect(a.hdl, id);
          }
        } else if (a.type == IN_MESSAGE) {
          spdlog::trace("processing IN_MESSAGE action");
          unique_lock<mutex> conn_lock(m_connection_lock);

          auto it = m_connection_ids.find(a.hdl);
          if(it == m_connection_ids.end()) {
            spdlog::trace(
                "recieved message from client hdl {} w/no id: {}",
                a.hdl.lock().get(),
                a.msg
              );
            conn_lock.unlock();
            open_session(a.hdl, a.msg);
          } else {
            combined_id id = it->second;
            conn_lock.unlock();

            spdlog::trace(
                "player {} with session {} sent: {}",
                id.player,
                id.session,
                a.msg
              );

            m_handle_message(id, std::move(a.msg));
          }
        } else if(a.type == OUT_MESSAGE) {
          spdlog::trace("processing OUT_MESSAGE action");
          spdlog::trace(
              "sending message to client hdl {}: {}",
              a.hdl.lock().get(),
              a.msg
            );
          send_to_hdl(a.hdl, a.msg);
        } else if(a.type == CLOSE_CONNECTION) { 
          spdlog::trace("processing CLOSE_CONNECTION action");
          spdlog::trace(
              "closing client hdl {} with final message: {}",
              a.hdl.lock().get(),
              a.msg
            );

          send_to_hdl(a.hdl, a.msg);
          close_hdl(a.hdl, close_reasons::session_complete());
        } else {
          // undefined.
        }
      }
    }

    std::size_t get_player_count() {
      lock_guard<mutex> guard(m_connection_lock);
      return m_connection_ids.size();
    }

    void send_message(const combined_id& id, std::string&& msg) {
      connection_hdl hdl;
      if(get_connection_hdl_from_id(hdl, id)) {
        {
          lock_guard<mutex> guard(m_action_lock);
          spdlog::trace("out_message: {}", msg);
          m_actions.push(
              action{OUT_MESSAGE, hdl, std::move(msg)}
            );
        }
        m_action_cond.notify_one();
      } else {
        spdlog::trace(
            "ignored message sent to player {} with session {}: connection closed",
            id.player, id.session
          );
      }
    }

    void complete_session(
        const session_id& sid,
        const session_id& result_sid,
        json&& result_data
      )
    {
      unique_lock<mutex> lock(m_session_lock);
      update_session_locks();
      if(!m_locked_sessions.contains(sid)) {
        spdlog::trace("completing session {}", sid);
        session_data result{result_sid, std::move(result_data)};

        auto it = m_session_players.find(sid);
        if(it != m_session_players.end()) {
          if(it->second.size() > 0) {
            for(const player_id& pid : it->second) {
              combined_id id{ pid, sid };

              connection_hdl hdl;
              if(get_connection_hdl_from_id(hdl, id)) {
                {
                  lock_guard<mutex> guard(m_action_lock);
                  spdlog::trace("closing session {} player {}", sid, pid);
                  m_actions.push(action(
                        CLOSE_CONNECTION,
                        hdl,
                        m_get_result_str(
                          { id.player, result.session },
                          result.data
                        )
                      ));
                }
                m_action_cond.notify_one();
              } else {
                spdlog::trace(
                    "can't close player {} session {}: connection already closed",
                    id.player,
                    id.session
                  );
              }
            }
          }
        }

        m_locked_sessions.insert(
            std::make_pair(sid, std::move(result))
          );
      }
    }

  private:
    void player_disconnect(connection_hdl hdl, const combined_id& id) {
      {
        lock_guard<mutex> connection_guard(m_connection_lock);
        m_connection_ids.erase(hdl);
        m_id_connections.erase(id);
      }
      {
        lock_guard<mutex> session_guard(m_session_lock);
        auto it = m_session_players.find(id.session);
        if(it != m_session_players.end()) {
          it->second.erase(id.player);
          if(it->second.empty()) {
            m_session_players.erase(it);
          }
        }
      }

      spdlog::debug("player {} with session {} disconnected",
          id.player, id.session);
      m_handle_close(id);
    }

    bool get_connection_hdl_from_id(
        connection_hdl& hdl,
        const combined_id& id
      )
    {
      lock_guard<mutex> guard(m_connection_lock);
      auto it = m_id_connections.find(id);
      if(it != m_id_connections.end()) {
        hdl = it->second;
        return true;
      }

      return false;
    }

    void send_to_hdl(connection_hdl hdl, const std::string& msg) {
      try {
        m_server.send(hdl, msg, websocketpp::frame::opcode::text);
      } catch (std::exception& e) {
        spdlog::debug(
            "error sending message \"{}\": {}",
            msg,
            e.what()
          );
      }
    }

    void close_hdl(connection_hdl hdl, const std::string& reason) {
      try {
        m_server.close(
            hdl,
            websocketpp::close::status::normal,
            reason
          );
      } catch (std::exception& e) {
        spdlog::debug("error closing connection: {}",
            e.what());
      }
    }

    void on_open(connection_hdl hdl) {
      {
        lock_guard<mutex> guard(m_action_lock);
        if(m_is_running) {
          m_actions.push(action(SUBSCRIBE,hdl));
        } else {
          close_hdl(hdl, close_reasons::server_shutdown());
        }
      }
      m_action_cond.notify_one();
    }

    void on_close(connection_hdl hdl) {
      {
        lock_guard<mutex> guard(m_action_lock);
        m_actions.push(action(UNSUBSCRIBE, hdl));
      }
      m_action_cond.notify_one();
    }

    void on_message(connection_hdl hdl, message_ptr msg) {
      {
        lock_guard<mutex> guard(m_action_lock);
        m_actions.push(action(IN_MESSAGE, hdl, std::move(msg->get_raw_payload())));
      }
      m_action_cond.notify_one();
    }

    // assumes that m_session_lock is acquired
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

    void setup_connection_id(connection_hdl hdl, const combined_id& id) {
      lock_guard<mutex> connection_guard(m_connection_lock);

      // immediately close duplicate connections to avoid complications
      auto id_connections_it = m_id_connections.find(id);
      if(id_connections_it != m_id_connections.end()) {
        spdlog::debug(
            "closing duplicate connection for player {} session {}",
            id.player,
            id.session
          );

        close_hdl(id_connections_it->second, close_reasons::duplicate_connection());

        m_connection_ids.erase(id_connections_it->second);
        m_id_connections.erase(id_connections_it);
      }

      m_connection_ids.emplace(hdl, id);
      m_id_connections.emplace(id, hdl);
      m_new_connections.erase(hdl);
    }

    void open_session(connection_hdl hdl, const std::string& login_token) {
      combined_id id;
      json login_json;
      bool completed = false;
      try {
        jwt::decoded_jwt<json_traits> decoded_token =
          jwt::decode<json_traits>(login_token);
        m_jwt_verifier.verify(decoded_token);
        auto claim_map = decoded_token.get_payload_claims();
        player_id pid = player_traits::parse_player_id(
            claim_map.at("pid").to_json()
          );
        session_id sid = player_traits::parse_session_id(
            claim_map.at("sid").to_json()
          );
        login_json = claim_map.at("data").to_json();
        id = combined_id{pid, sid};
        completed = true;
      } catch(std::out_of_range& e) {
        spdlog::debug(
            "connection provided jwt without id and/or data claims: {}",
            e.what()
          );
      } catch(jwt::error::token_verification_exception& e) {
        spdlog::debug(
            "connection provided jwt with invalid signature: {}",
            e.what()
          );
      } catch(std::invalid_argument& e) {
        spdlog::debug(
            "connection provided invalid jwt token string: {}",
            e.what()
          );
      } catch(std::exception& e) {
        spdlog::debug("connection provided jwt with invalid claims: {}", e.what());
      }

      if(completed) {
        lock_guard<mutex> session_guard(m_session_lock);
        update_session_locks();

        if(!m_locked_sessions.contains(id.session)) {
          setup_connection_id(hdl, id);
          m_session_players[id.session].insert(id.player);
          spdlog::debug(
              "player {} connected with session {}: {}",
              id.player,
              id.session,
              login_json.dump()
            );
          m_handle_open(id, std::move(login_json));
        } else {
          send_to_hdl(
              hdl,
              m_get_result_str(
                  { id.player, m_locked_sessions.at(id.session).session },
                  m_locked_sessions.at(id.session).data
                )
            );
          close_hdl(hdl, close_reasons::session_complete());
        }
      } else {
        close_hdl(hdl, close_reasons::invalid_jwt());
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
    unordered_map<combined_id, connection_hdl, id_hash> m_id_connections;

    // m_connection_lock guards the members m_new_connections,
    // m_id_connections, and m_connection_ids
    mutex m_connection_lock;

    time_point m_last_session_update_time;
    std::chrono::milliseconds m_session_release_time;

    buffered_map<
        unordered_map<session_id, session_data, id_hash>
      > m_locked_sessions;
    unordered_map<session_id, set<player_id>, id_hash> m_session_players;

    // m_session_lock guards the members m_locked_sessions, and
    // m_session_players
    mutex m_session_lock;

    queue<action> m_actions;

    // m_action_lock guards the member m_actions
    mutex m_action_lock;
    condition_variable m_action_cond;

    // functions to handle client actions
    function<void(const combined_id&, json&&)> m_handle_open;
    function<void(const combined_id&)> m_handle_close;
    function<void(const combined_id&, std::string&&)> m_handle_message;
  };
}

#endif // JWT_GAME_SERVER_BASE_SERVER_HPP
