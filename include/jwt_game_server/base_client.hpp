#ifndef JWT_GAME_SERVER_BASE_CLIENT_HPP
#define JWT_GAME_SERVER_BASE_CLIENT_HPP

#include <websocketpp/client.hpp>

#include <jwt-cpp/jwt.h>

#include <spdlog/spdlog.h>

namespace jwt_game_server {
  // websocketpp types
  using websocketpp::connection_hdl;

  // functional types
  using std::bind;
  using std::placeholders::_1;
  using std::placeholders::_2;

  template<typename client_config>
  class base_client {
  // type definitions
  private:
    using ws_client = websocketpp::client<client_config>;
    using message_ptr = typename ws_client::message_ptr;

  // main class body
  public:
    base_client(const std::string& uri, const std::string& jwt)
        : m_is_running(false), m_uri(uri), m_jwt(jwt) {
      m_client.init_asio();

      m_client.set_open_handler(bind(&base_client::on_open, this,
        jwt_game_server::_1));
      m_client.set_close_handler(bind(&base_client::on_close, this,
        jwt_game_server::_1));
      m_client.set_message_handler(
          bind(&base_client::on_message, this, jwt_game_server::_1,
            jwt_game_server::_2)
        );
    }

    base_client(const base_client& c) : base_client(c.m_uri, c.m_jwt) {}

    void connect() {
      websocketpp::lib::error_code ec;
      m_connection = m_client.get_connection(m_uri, ec);
      if(ec) {
        spdlog::debug(ec.message());
      } else {
        try {
          m_client.connect(m_connection);
          m_is_running = true;
          m_client.run();
        } catch(std::exception& e) {
          m_is_running = false;
          spdlog::error("error opening client connection: {}", e.what());
        }
      }
    }

    bool is_running() {
      return m_is_running;
    }

    void disconnect() {
      if(m_is_running) {
        try {
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

  protected:
    virtual void on_open(connection_hdl hdl) {
      spdlog::trace("client connection opened");
      this->send(m_jwt);
    }

    virtual void on_close(connection_hdl hdl) {
      spdlog::trace("client connection closed");
      m_is_running = false;
    }

    virtual void on_message(connection_hdl hdl, message_ptr msg) {
      spdlog::trace("client received message: {}", msg->get_payload());
    }

    // member variables
    ws_client m_client;
    typename ws_client::connection_ptr m_connection;
    bool m_is_running;
    std::string m_uri;
    std::string m_jwt;
  };
}

#endif // JWT_GAME_SERVER_BASE_CLIENT_HPP
