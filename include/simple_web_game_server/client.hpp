/*
 * Copyright (c) 2020 Daniel Aven Bross
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef JWT_GAME_SERVER_BASE_CLIENT_HPP
#define JWT_GAME_SERVER_BASE_CLIENT_HPP

#include <websocketpp/client.hpp>

#include <jwt-cpp/jwt.h> 
#include <spdlog/spdlog.h>

#include <atomic>
#include <mutex>
#include <functional>

#include <exception>

namespace simple_web_game_server {
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

  /// A simple WebSocket client to connect to an instance of base_server.
  template<typename client_config>
  class client {
  // type definitions
  private:
    using ws_client = websocketpp::client<client_config>;
    using message_ptr = typename ws_client::message_ptr;

  public:
    class client_error : public std::runtime_error {
    public:
      using super = std::runtime_error;
      explicit client_error(const std::string& what_arg) noexcept :
        super(what_arg) {}
      explicit client_error(const char* what_arg) noexcept : super(what_arg) {}
    };

  // main class body
  public:
    /// Constructs the client with empty handler functions.
    client() : m_is_running{false}, m_has_failed{false},
      m_handle_open{[](){}}, m_handle_close{[](){}},
      m_handle_message{[](const std::string& s){}}
    {
      m_client.init_asio();

      m_client.set_open_handler(bind(&client::on_open, this,
        simple_web_game_server::_1));
      m_client.set_close_handler(bind(&client::on_close, this,
        simple_web_game_server::_1));
      m_client.set_message_handler(
          bind(&client::on_message, this, simple_web_game_server::_1,
            simple_web_game_server::_2)
        );
    }

    /// Constructs the client with the given handler functions.
    client(
        std::function<void()> of,
        std::function<void()> cf,
        std::function<void(const std::string&)> mf
      ) : m_is_running{false}, m_has_failed{false},
          m_handle_open{of}, m_handle_close{cf},
          m_handle_message{mf}
    {
      m_client.init_asio();

      m_client.set_open_handler(bind(&client::on_open, this,
        simple_web_game_server::_1));
      m_client.set_close_handler(bind(&client::on_close, this,
        simple_web_game_server::_1));
      m_client.set_message_handler(
          bind(&client::on_message, this, simple_web_game_server::_1,
            simple_web_game_server::_2)
        );
    }

    /// Connects to a server at the given URI and sends the given string.
    void connect(const std::string& uri, const std::string& jwt) {
      if(m_is_running) {
        throw client_error("connect called on running client");
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

    /// Returns whether the underlying WebSocket client is running.
    bool is_running() {
      return m_is_running;
    }

    /// Returns whether the connection has failed.
    bool has_failed() {
      return m_has_failed;
    }

    /// Close the connection to the server.
    void disconnect() {
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
      } else {
        throw client_error("disconnect called on stopped client");
      }
    }

    /// Resets the client so it may connect to a new server.
    void reset() {
      if(m_is_running) {
        this->disconnect();
      }
      m_client.reset();
    }

    /// Synchronously send the given string to the server.
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
        throw client_error{
            std::string{"send called on stopped client with message: "} + 
            msg
          };
      }
    }

    /// Sets the given function to be called when the client connects.
    void set_open_handler(std::function<void()> f) {
      if(!m_is_running) {
        m_handle_open = f;
      } else {
        throw client_error{"set_open_handler called on running client"};
      }
    }

    /// Sets the given function to be called when the client disconnects.
    void set_close_handler(std::function<void()> f) {
      if(!m_is_running) {
        m_handle_close = f;
      } else { 
        throw client_error{"set_close_handler called on running client"};
      }
    }

    /// Sets the given function to be called when the client gets a message.
    void set_message_handler(std::function<void(const std::string&)> f) {
      if(!m_is_running) {
        m_handle_message = f;
      } else {
        throw client_error{"set_message_handler called on running client"};
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
