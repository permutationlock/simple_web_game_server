#include <doctest/doctest.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/ostream_sink.h>

#define DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

#include <jwt_game_server/client.hpp>
#include <model_games/minimal_game.hpp>

#include <websocketpp/server.hpp>
#include <websocketpp_configs/asio_no_logs.hpp>
#include <websocketpp_configs/asio_client_no_logs.hpp>

#include <atomic>
#include <thread>
#include <functional>
#include <sstream>
#include <chrono>

#include "constants.hpp"

TEST_CASE("the base client should interact with a websocket server") {
  using namespace std::chrono_literals;

  using ws_server = websocketpp::server<asio_no_logs>;

  using message_ptr = typename ws_server::message_ptr;
  using connection_hdl = websocketpp::connection_hdl;

  using std::placeholders::_1;
  using std::placeholders::_2;

  using ws_client = jwt_game_server::client<
      asio_client_no_logs
    >;

  // setup logging sink to track errors
  std::ostringstream oss;
  //auto ostream_sink = std::make_shared<spdlog::sinks::ostream_sink_mt> (oss);
  //auto logger = std::make_shared<spdlog::logger>("my_logger", ostream_sink);
  //spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::err);

  struct test_data {
    void on_open(connection_hdl hdl) {
      client_hdl = hdl;
      conn_open = true;
    }

    void on_close(connection_hdl hdl) {
      conn_open = false;
    }

    void on_message(connection_hdl hdl, message_ptr msg) {
      last_message = msg->get_payload();
      messages.push_back(last_message);
    }

    bool conn_open;
    connection_hdl client_hdl; 
    std::vector<std::string> messages;
    std::string last_message;
  };

  test_data server_data;
  ws_server server;
  server.init_asio();

  server.set_reuse_addr(true);
  server.set_open_handler(
      std::bind(&test_data::on_open, &server_data, _1)
    );
  server.set_close_handler(
      std::bind(&test_data::on_close, &server_data, _1)
    );
  server.set_message_handler(
      std::bind(&test_data::on_message, &server_data, _1, _2)
    );

  server.listen(SERVER_PORT);
  server.start_accept();
  std::thread server_thr{std::bind(&ws_server::run, &server)};

  std::string uri = std::string{"ws://localhost:"}
    + std::to_string(SERVER_PORT);

  std::this_thread::sleep_for(100ms);

  ws_client client;

  SUBCASE("the client should be running only when it is connected") {
    CHECK(server_data.conn_open == false);
    CHECK(client.is_running() == false);
    
    std::thread client_thr{std::bind(&ws_client::connect, &client, uri, "")};
    while(!client.is_running() && !client.has_failed()) {
      std::this_thread::sleep_for(10ms);
    }
    
    std::this_thread::sleep_for(100ms);

    CHECK(server_data.conn_open == true);
    CHECK(client.is_running() == true);
    CHECK(oss.str() == std::string{""});
    CHECK(client.has_failed() == false);

    if(client.is_running()) {
      client.disconnect(); 
    }

    while(client.is_running() && !client.has_failed()) {
      std::this_thread::sleep_for(10ms);
    }

    std::this_thread::sleep_for(100ms);

    CHECK(server_data.conn_open == false);
    CHECK(client.is_running() == false);
    CHECK(client.has_failed() == false);

    CHECK(oss.str() == std::string{""});
    client_thr.join();
  }

  SUBCASE("the client should send its JWT upon opening the connection") { 
    std::string token{"JWT"};

    CHECK(server_data.conn_open == false);

    std::thread client_thr{
        std::bind(&ws_client::connect, &client, uri, token)
      };

    while(!client.is_running() && !client.has_failed()) {
      std::this_thread::sleep_for(10ms);
    }
    std::this_thread::sleep_for(100ms);

    CHECK(server_data.conn_open == true);
    CHECK(server_data.messages.size() == 1);
    CHECK(server_data.last_message == token);
    CHECK(oss.str() == std::string{""});
    CHECK(client.has_failed() == false);

    if(client.is_running()) {
      client.disconnect(); 
    }

    while(client.is_running() && !client.has_failed()) {
      std::this_thread::sleep_for(10ms);
    }
    std::this_thread::sleep_for(100ms);

    CHECK(server_data.conn_open == false);
    CHECK(oss.str() == std::string{""});
    CHECK(client.has_failed() == false);
    client_thr.join();
  }

  SUBCASE("the client should be able to send messages to the server") {
    CHECK(server_data.conn_open == false);

    std::thread client_thr{
        std::bind(&ws_client::connect, &client, uri, "1234")
      };

    while(!client.is_running() && !client.has_failed()) {
      std::this_thread::sleep_for(10ms);
    }

    std::string message{"abcd"};
    client.send(message);
    std::this_thread::sleep_for(100ms);

    CHECK(server_data.conn_open == true);
    CHECK(server_data.messages.size() == 2);
    CHECK(server_data.last_message == message);
    CHECK(oss.str() == std::string{""});
    CHECK(client.has_failed() == false);

    if(client.is_running()) {
      client.disconnect(); 
    }

    while(client.is_running() && !client.has_failed()) {
      std::this_thread::sleep_for(10ms);
    }
    std::this_thread::sleep_for(100ms);

    CHECK(server_data.conn_open == false);
    CHECK(oss.str() == std::string{""});
    CHECK(client.has_failed() == false);
    client_thr.join();
  }

  SUBCASE("the client should be able to bind a function to handle open") { 
    struct test_client_data {
      void on_open() {
        has_opened = true;
      }
      
      bool has_opened = false;
    };

    test_client_data client_data;

    auto open_handler = std::bind(&test_client_data::on_open, &client_data);
    client.set_open_handler(open_handler);

    CHECK(oss.str() == std::string{""});

    std::thread client_thr{
        std::bind(&ws_client::connect, &client, uri, "1234")
      };

    while(!client.is_running() && !client.has_failed()) {
      std::this_thread::sleep_for(10ms);
    }

    std::this_thread::sleep_for(100ms);

    CHECK(server_data.conn_open == true);
    CHECK(client_data.has_opened == true);
    CHECK(oss.str() == std::string{""});
    CHECK(client.has_failed() == false);

    if(client.is_running()) {
      client.disconnect(); 
    }

    while(client.is_running() && !client.has_failed()) {
      std::this_thread::sleep_for(10ms);
    }
    std::this_thread::sleep_for(100ms);

    CHECK(server_data.conn_open == false);
    CHECK(oss.str() == std::string{""});
    CHECK(client.has_failed() == false);
    client_thr.join();
  }

  SUBCASE("the client should be able to bind a function to handle close") { 
    struct test_client_data {
      void on_close() {
        has_closed = true;
      }
      
      bool has_closed = false;
    };

    test_client_data client_data;

    auto close_handler = std::bind(&test_client_data::on_close, &client_data);
    client.set_close_handler(close_handler);

    CHECK(oss.str() == std::string{""});

    std::thread client_thr{
        std::bind(&ws_client::connect, &client, uri, "1234")
      };

    while(!client.is_running() && !client.has_failed()) {
      std::this_thread::sleep_for(10ms);
    }

    std::this_thread::sleep_for(100ms);

    CHECK(server_data.conn_open == true);
    CHECK(client_data.has_closed == false);
    CHECK(oss.str() == std::string{""});
    CHECK(client.has_failed() == false);

    if(client.is_running()) {
      client.disconnect(); 
    }

    while(client.is_running() && !client.has_failed()) {
      std::this_thread::sleep_for(10ms);
    }
    std::this_thread::sleep_for(100ms);

    CHECK(server_data.conn_open == false);
    CHECK(client_data.has_closed == true);
    CHECK(oss.str() == std::string{""});
    CHECK(client.has_failed() == false);
    client_thr.join();
  }

  SUBCASE("the client should be able to bind a function to handle messages") { 
    struct test_client_data {
      void on_message(const std::string& message) {
        last_message = message;
      }
      
      std::string last_message;
    };

    test_client_data client_data;

    auto message_handler = std::bind(&test_client_data::on_message,
        &client_data, _1);
    client.set_message_handler(message_handler);

    CHECK(oss.str() == std::string{""});

    std::thread client_thr{
        std::bind(&ws_client::connect, &client, uri, "1234")
      };

    while(!client.is_running() && !client.has_failed()) {
      std::this_thread::sleep_for(10ms);
    }

    std::this_thread::sleep_for(100ms);

    CHECK(server_data.conn_open == true);
    CHECK(oss.str() == std::string{""});
    CHECK(client.has_failed() == false);

    std::string message{"a093u1jdafasldfh"};
    server.send(server_data.client_hdl, message,
      websocketpp::frame::opcode::text);

    std::this_thread::sleep_for(100ms);

    CHECK(client_data.last_message == message);
    CHECK(oss.str() == std::string{""});
    CHECK(client.has_failed() == false);

    if(client.is_running()) {
      client.disconnect(); 
    }

    while(client.is_running() && !client.has_failed()) {
      std::this_thread::sleep_for(10ms);
    }
    std::this_thread::sleep_for(100ms);

    CHECK(server_data.conn_open == false);
    CHECK(oss.str() == std::string{""});
    CHECK(client.has_failed() == false);
    client_thr.join();
  }

  SUBCASE("the client should be able to reset and reconnect as usual") { 
    std::string token{"my_jwt"};
    std::thread client_thr{
        std::bind(&ws_client::connect, &client, uri, token)
      };
    while(!client.is_running() && !client.has_failed()) {
      std::this_thread::sleep_for(10ms);
    }

    std::this_thread::sleep_for(100ms);

    if(client.is_running()) {
      client.disconnect(); 
    }

    while(client.is_running() && !client.has_failed()) {
      std::this_thread::sleep_for(10ms);
    }

    client_thr.join();

    CHECK(server_data.conn_open == false);
    CHECK(client.is_running() == false);

    client.reset();

    client_thr = std::thread{
        std::bind(&ws_client::connect, &client, uri, token)
      };
    while(!client.is_running() && !client.has_failed()) {
      std::this_thread::sleep_for(10ms);
    }
    
    std::this_thread::sleep_for(100ms);

    CHECK(server_data.conn_open == true);
    CHECK(server_data.messages.size() == 2);
    CHECK(server_data.last_message == token);
    CHECK(client.is_running() == true);
    CHECK(oss.str() == std::string{""});
    CHECK(client.has_failed() == false);

    if(client.is_running()) {
      client.disconnect(); 
    }

    while(client.is_running() && !client.has_failed()) {
      std::this_thread::sleep_for(10ms);
    }

    std::this_thread::sleep_for(100ms);

    CHECK(server_data.conn_open == false);
    CHECK(client.is_running() == false);
    CHECK(client.has_failed() == false);

    CHECK(oss.str() == std::string{""});
    client_thr.join();
  }

  server.stop_listening();
  server_thr.join();
}
