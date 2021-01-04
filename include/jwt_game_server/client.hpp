#ifndef JWT_GAME_SERVER_BASE_CLIENT_HPP
#define JWT_GAME_SERVER_BASE_CLIENT_HPP

#include <websocketpp/client.hpp>

#include <jwt-cpp/jwt.h> 
#include <spdlog/spdlog.h>

#include <atomic>
#include <mutex>
#include <functional>

namespace jwt_game_server {
  // websocketpp types
  using websocketpp::connection_hdl;

  // functional types
  using std::function;
  using std::bind;
  using std::placeholders::_1;
  using std::placeholders::_2;

  // threading type implementations
  using std::atomic;
  using std::mutex;
  using std::lock_guard;

  template<typename client_config>
  class client {
  // type definitions
  private:
    using ws_client = websocketpp::client<client_config>;
    using message_ptr = typename ws_client::message_ptr;

  // main class body
  public:
    client()
        : m_is_running(false), m_has_failed(false), m_handle_open([](){}),
          m_handle_close([](){}),
          m_handle_message([](const std::string& s){}) {
      m_client.init_asio();

      m_client.set_open_handler(bind(&client::on_open, this,
        jwt_game_server::_1));
      m_client.set_close_handler(bind(&client::on_close, this,
        jwt_game_server::_1));
      m_client.set_message_handler(
          bind(&client::on_message, this, jwt_game_server::_1,
            jwt_game_server::_2)
        );
    }

    client(const client& c) : client() {}

    void connect(const std::string& uri, const std::string& jwt) {
      if(m_is_running) {
        spdlog::debug("cannot connect: client already connected");
        return;
      }

      m_jwt = jwt;

      websocketpp::lib::error_code ec;
      m_connection = m_client.get_connection(uri, ec);
      if(ec) {
        spdlog::debug(ec.message());
      } else {
        try {
          m_client.connect(m_connection);
          m_is_running = true;
          m_has_failed = false;
          m_client.run();
        } catch(std::exception& e) {
          m_is_running = false;
          m_has_failed = true;
          spdlog::error("error with client connection: {}", e.what());
        }
      }
    }

    bool is_running() {
      return m_is_running;
    }

    bool has_failed() {
      return m_has_failed;
    }

    void disconnect() {
      lock_guard<mutex> guard(m_connection_lock);
      if(m_is_running) {
        try {
          spdlog::trace("closing client connection");
          m_connection->close(
              websocketpp::close::status::normal,
              "client closed connection"
            );
        } catch(std::exception& e) {
          spdlog::error("error closing client connection: {}", e.what());
        }
      }
    }

    void send(const std::string& msg) {
      lock_guard<mutex> guard(m_connection_lock);
      if(m_is_running) {
        try {
          m_connection->send(
              msg,
              websocketpp::frame::opcode::text
            );
          spdlog::debug("client sent message: {}", msg);
        } catch(std::exception& e) {
          spdlog::error("error sending client message \"{}\": {}", msg,
            e.what());
        }
      } else {
          spdlog::error("client is not running, cannot send message \"{}\"",
            msg);
      }
    }

    void set_open_handler(std::function<void()> f) {
      if(!m_is_running) {
        m_handle_open = f;
      } else {
        spdlog::error("client cannot bind open handler while running");
      }
    }

    void set_close_handler(std::function<void()> f) {
      if(!m_is_running) {
        m_handle_close = f;
      } else { 
        spdlog::error("client cannot bind close handler while running");
      }
    }

    void set_message_handler(std::function<void(const std::string&)> f) {
      if(!m_is_running) {
        m_handle_message = f;
      } else {
        spdlog::error("client cannot bind message handler while running");
      }
    }

  private:
    void on_open(connection_hdl hdl) {
      spdlog::trace("client connection opened");
      this->send(m_jwt);
      try {
        m_handle_open();
      } catch(std::exception& e) {
        spdlog::error("error in open handler: {}", e.what());
      }
    }

    void on_close(connection_hdl hdl) {
      spdlog::trace("client connection closed");
      m_is_running = false;
      try {
        m_handle_close();
      } catch(std::exception& e) {
        spdlog::error("error in close handler: {}", e.what());
      }
    }

    void on_message(connection_hdl hdl, message_ptr msg) {
      spdlog::trace("client received message: {}", msg->get_payload());
      try {
        m_handle_message(msg->get_payload());
      } catch(std::exception& e) {
        spdlog::error("error in message handler: {}", e.what());
      }
    }

    // member variables
    mutex m_connection_lock;

    ws_client m_client;
    typename ws_client::connection_ptr m_connection;
    std::atomic<bool> m_is_running;
    std::atomic<bool> m_has_failed;
    std::string m_jwt;
    function<void()> m_handle_open;
    function<void()> m_handle_close;
    function<void(const std::string&)> m_handle_message;
  };
}

#endif // JWT_GAME_SERVER_BASE_CLIENT_HPP
