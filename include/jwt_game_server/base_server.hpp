#ifndef JWT_GAME_SERVER_BASE_SERVER_HPP
#define JWT_GAME_SERVER_BASE_SERVER_HPP

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <jwt-cpp/jwt.h>

#include <spdlog/spdlog.h>

#include <atomic>
#include <mutex>
#include <condition_variable>

namespace jwt_game_server {
  // websocketpp types
  using websocketpp::connection_hdl;

  // functional types
  using std::bind;
  using std::placeholders::_1;
  using std::placeholders::_2;

  // datatype implementations
  using std::vector;
  using std::map;
  using std::queue;

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
    using player_id = typename player_traits::player_id;
    using json = typename json_traits::json;

  private:
    enum action_type {
      SUBSCRIBE,
      UNSUBSCRIBE,
      IN_MESSAGE,
      OUT_MESSAGE
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

  // main class body
  public:
    base_server(const jwt::verifier<jwt_clock, json_traits>& v)
        : m_is_running(false), m_jwt_verifier(v) {
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
      m_is_running = true;

      try {
        m_server.set_reuse_addr(unlock_address);
        m_server.listen(port);
        m_server.start_accept();

        m_server.run();
      } catch (std::exception& e) {
        m_is_running = false;
        spdlog::error("error running server: {}", e.what());
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
    
    void stop() {      lock_guard<mutex> guard(m_connection_lock);
      m_is_running = false;
      m_server.stop_listening();

      for(auto& con_pair : m_connection_ids) {
        connection_hdl hdl = con_pair.first;
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
        } else if (a.type == UNSUBSCRIBE) {
          spdlog::trace("processing UNSUBSCRIBE action");
          unique_lock<mutex> conn_lock(m_connection_lock);
          if(m_connection_ids.count(a.hdl) < 1) {
            spdlog::trace("player disconnected without providing id");
          } else {
            // connection provided a player id
            conn_lock.unlock();
            this->player_disconnect(a.hdl);
          }
        } else if (a.type == IN_MESSAGE) {
          spdlog::trace("processing IN_MESSAGE action");
          unique_lock<mutex> conn_lock(m_connection_lock);

          if(m_connection_ids.count(a.hdl) < 1) {
            spdlog::trace("recieved message from connection w/no id");
            conn_lock.unlock();

            try {
              this->setup_player(a.hdl, a.msg);
            } catch(std::exception& e) {
              spdlog::debug("error setting up id: {}", e.what());
            }
          } else {
            player_id id = m_connection_ids[a.hdl];
            conn_lock.unlock();

            spdlog::trace("received message from id: {}", id);
            try {
              this->process_message(id, a.msg);
            } catch(std::exception& e) {
              spdlog::debug("error processing message: {}", e.what());
            }
          }
        } else if(a.type == OUT_MESSAGE) { 
          spdlog::trace("processing OUT_MESSAGE action");
          try {
            lock_guard<mutex> guard(m_connection_lock);
            m_server.send(a.hdl, a.msg, websocketpp::frame::opcode::text);
          } catch (std::exception& e) {
            spdlog::debug(
                "error sending message \"{}\": {}",
                a.msg,
                e.what()
              );
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
    void send_message(connection_hdl hdl, const std::string& msg) {
      {
        lock_guard<mutex> guard(m_action_lock);
        spdlog::trace("out_message: {}", msg);
        m_actions.push(action(OUT_MESSAGE, hdl, msg));
      }
      m_action_cond.notify_one();
    }

    void close_connection(connection_hdl hdl, const std::string& reason) {
      lock_guard<mutex> guard(m_connection_lock);
      try {
        m_server.close(
            hdl,
            websocketpp::close::status::normal,
            reason
          );
      } catch (std::exception& e) {
        spdlog::debug("error closing connection: {}", e.what());
      }
    }

    virtual void process_message(player_id, const std::string& text) {}

    virtual void player_connect(connection_hdl hdl, player_id id, const json& data) {
      lock_guard<mutex> guard(m_connection_lock);
      m_connection_ids[hdl] = id;

      spdlog::debug("player {} connected", id);
    }

    virtual player_id player_disconnect(connection_hdl hdl) {
      lock_guard<mutex> connection_guard(m_connection_lock);
      player_id id = m_connection_ids[hdl];
      m_connection_ids.erase(hdl);

      spdlog::debug("player {} disconnected", id);
      return id;
    }

  private:
    void on_open(connection_hdl hdl) {
      {
        lock_guard<mutex> guard(m_action_lock);
        spdlog::trace("connection opened");
        m_actions.push(action(SUBSCRIBE,hdl));
      }
      m_action_cond.notify_one();
    }

    void on_close(connection_hdl hdl) {
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

    void setup_player(connection_hdl hdl, const std::string& token) {
      player_id id;
      json login_json;
      bool completed = false;
      try {
        jwt::decoded_jwt<json_traits> decoded_token =
          jwt::decode<json_traits>(token);
        auto claim_map = decoded_token.get_payload_claims();
        id = player_traits::parse_id(claim_map.at("id").to_json());
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
        spdlog::debug("error verifying player jwt: {}", e.what());
      }
      
      if(completed) {
        this->player_connect(hdl, id, login_json);
      }
    }

    // member functions 
    ws_server m_server;
    atomic<bool> m_is_running;

    jwt::verifier<jwt_clock, json_traits> m_jwt_verifier;

    map<
        connection_hdl,
        player_id,
        std::owner_less<connection_hdl>
      > m_connection_ids;
    mutex m_connection_lock;

    queue<action> m_actions;
    mutex m_action_lock;
    condition_variable m_action_cond;
  };
}

#endif // JWT_GAME_SERVER_BASE_SERVER_HPP
