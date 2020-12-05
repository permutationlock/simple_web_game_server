#ifndef JWT_GAME_SERVER_BASE_SERVER_HPP
#define JWT_GAME_SERVER_BASE_SERVER_HPP

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <jwt-cpp/jwt.h>

#include <spdlog/spdlog.h>

#include <mutex>
#include <condition_variable>

namespace jwt_game_server {
  using websocketpp::connection_hdl;

  // Define functional types
  using std::bind;
  using std::placeholders::_1;
  using std::placeholders::_2;

  // Define data type implementations
  using std::vector;
  using std::map;
  using std::queue;

  // Define threading type implementations
  using std::mutex;
  using std::lock_guard;
  using std::unique_lock;
  using std::condition_variable;

  // websocket server type
  typedef websocketpp::server<websocketpp::config::asio> wss_server;

  // on_message action for the server
  enum action_type {
    SUBSCRIBE,
    UNSUBSCRIBE,
    MESSAGE
  };

  struct action {
    action(action_type t, connection_hdl h) : type(t), hdl(h) {}
    action(action_type t, connection_hdl h, wss_server::message_ptr m)
      : type(t), hdl(h), msg(m) {}

    action_type type;
    connection_hdl hdl;
    wss_server::message_ptr msg;
  };

  template<typename player_traits, typename jwt_clock, typename json_traits>
  class base_server {
  public:
    typedef typename player_traits::player_id player_id;
    typedef typename json_traits::json json;

    base_server(const jwt::verifier<jwt_clock, json_traits>& v) : m_jwt_verifier(v) {
      // initialize Asio Transport
      m_server.init_asio();

      // register handler callbacks
      m_server.set_open_handler(bind(&base_server::on_open, this,
        jwt_game_server::_1));
      m_server.set_close_handler(bind(&base_server::on_close, this,
        jwt_game_server::_1));
      m_server.set_message_handler(
          bind(&base_server::on_message, this, jwt_game_server::_1,
            jwt_game_server::_2)
        );
    }

    void run(uint16_t port) {
      // listen on specified port
      m_server.listen(port);

      // start the server accept loop
      m_server.start_accept();

      // start the ASIO io_service run loop
      try {
        m_server.run();
      } catch (std::exception & e) {
        spdlog::debug(e.what());
      }
    }

    void process_messages() {
      while(1) {
        unique_lock<mutex> action_lock(m_action_lock);

        while(m_actions.empty()) {
          m_action_cond.wait(action_lock);
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
        } else if (a.type == MESSAGE) {
          spdlog::trace("processing MESSAGE action");
          unique_lock<mutex> conn_lock(m_connection_lock);

          if(m_connection_ids.count(a.hdl) < 1) {
            spdlog::trace("recieved message from connection w/no id");
            conn_lock.unlock();

            try {
              this->setup_player(a.hdl, a.msg->get_payload());
            } catch(std::exception& e) {
              spdlog::debug("error setting up id");
            }
          } else {
            // player id already setup, route to their game
            player_id id = m_connection_ids[a.hdl];
            conn_lock.unlock();

            spdlog::trace("received message from id: {}", id);
            try {
              this->process_message(id, a.msg->get_payload());
            } catch(std::exception& e) {
              spdlog::debug("error processing message");
            }
          }
        } else {
          // undefined.
        }
      }
    }

    virtual void process_message(player_id, const std::string& text) {
      spdlog::trace("  \'{}\'", text);
    }

    virtual void player_connect(connection_hdl hdl, player_id id, const json& data) {
      lock_guard<mutex> guard(m_connection_lock);
      m_connection_ids[hdl] = id;

      spdlog::debug("player {} connected", id);
    }

    virtual void player_disconnect(connection_hdl hdl) {
      lock_guard<mutex> connection_guard(m_connection_lock);
      player_id id = m_connection_ids[hdl];
      m_connection_ids.erase(hdl);

      spdlog::debug("player {} disconnected", id);
    }

  protected:
    typedef map<
        connection_hdl,
        player_id,
        std::owner_less<connection_hdl>
      > con_map;

    wss_server m_server;
    mutex m_connection_lock;
    con_map m_connection_ids;
    jwt::verifier<jwt_clock, json_traits> m_jwt_verifier;

  private:
    mutex m_action_lock;
    condition_variable m_action_cond;

    queue<action> m_actions;   

    void on_open(connection_hdl hdl) {
      {
        lock_guard<mutex> guard(m_action_lock);
        spdlog::trace("on_open");
        m_actions.push(action(SUBSCRIBE,hdl));
      }
      m_action_cond.notify_one();
    }

    void on_close(connection_hdl hdl) {
      {
        lock_guard<mutex> guard(m_action_lock);
        spdlog::trace("on_close");
        m_actions.push(action(UNSUBSCRIBE,hdl));
      }
      m_action_cond.notify_one();
    }

    void on_message(connection_hdl hdl, wss_server::message_ptr msg) {
      {
        lock_guard<mutex> guard(m_action_lock);
        spdlog::trace("on_message: {}", msg->get_payload());
        m_actions.push(action(MESSAGE,hdl,msg));
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
        spdlog::debug("connection provided jwt without id and/or data claim");
      } catch(jwt::error::token_verification_exception& e) { 
        spdlog::debug("connection provided jwt that could not be verified");
      } catch(std::invalid_argument& e) { 
        spdlog::debug("connection provided invalid jwt token string");
      } catch(std::runtime_error& e) {
        spdlog::debug("connection provided invalid json in jwt");
      } catch(std::exception& e) {
        spdlog::debug("unknown reason but invalid login jwt");
      }
      
      if(completed) {
        this->player_connect(hdl, id, login_json);
      }
    }
  };
}

#endif // JWT_GAME_SERVER_BASE_SERVER_HPP
