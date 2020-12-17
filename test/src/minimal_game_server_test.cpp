#include <doctest/doctest.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/ostream_sink.h>

#define DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

#include <jwt_game_server/game_server.hpp>
#include <jwt_game_server/base_client.hpp>
#include <json_traits/nlohmann_traits.hpp>
#include <model_games/minimal_game.h>

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>

#include <thread>
#include <functional>
#include <sstream>
#include <chrono>

struct server_config : public websocketpp::config::asio {
  typedef server_config type;
  typedef websocketpp::config::asio base;

  typedef base::concurrency_type concurrency_type;

  typedef base::request_type request_type;
  typedef base::response_type response_type;

  typedef base::message_type message_type;
  typedef base::con_msg_manager_type con_msg_manager_type;
  typedef base::endpoint_msg_manager_type endpoint_msg_manager_type;

  typedef base::alog_type alog_type;
  typedef base::elog_type elog_type;

  typedef base::rng_type rng_type;

  struct transport_config : public base::transport_config {
    typedef type::concurrency_type concurrency_type;
    typedef type::alog_type alog_type;
    typedef type::elog_type elog_type;
    typedef type::request_type request_type;
    typedef type::response_type response_type;
    typedef websocketpp::transport::asio::basic_socket::endpoint socket_type;
  };

  typedef websocketpp::transport::asio::endpoint<transport_config>
    transport_type;

  static const websocketpp::log::level elog_level = 
    websocketpp::log::elevel::none;

  static const websocketpp::log::level alog_level =
    websocketpp::log::alevel::none;
};

struct client_config : public websocketpp::config::core_client {
  typedef client_config type;
  typedef websocketpp::config::core_client base;

  typedef base::concurrency_type concurrency_type;

  typedef base::request_type request_type;
  typedef base::response_type response_type;

  typedef base::message_type message_type;
  typedef base::con_msg_manager_type con_msg_manager_type;
  typedef base::endpoint_msg_manager_type endpoint_msg_manager_type;

  typedef base::alog_type alog_type;
  typedef base::elog_type elog_type;

  typedef base::rng_type rng_type;

  struct transport_config : public base::transport_config {
    typedef type::concurrency_type concurrency_type;
    typedef type::alog_type alog_type;
    typedef type::elog_type elog_type;
    typedef type::request_type request_type;
    typedef type::response_type response_type;
    typedef websocketpp::transport::asio::basic_socket::endpoint
      socket_type;
  };

  typedef websocketpp::transport::asio::endpoint<transport_config>
    transport_type;

  static const websocketpp::log::level elog_level = 
    websocketpp::log::elevel::none;

  static const websocketpp::log::level alog_level =
    websocketpp::log::alevel::none;
};

TEST_CASE("the server should start and shutdown without exceptions") {
  using namespace std::chrono_literals;

  typedef jwt_game_server::base_client<
      client_config
    > minimal_game_client;

  typedef jwt_game_server::game_server<
      minimal_game,
      jwt::default_clock,
      nlohmann_traits,
      server_config
    > minimal_game_server;

  // setup logging sink to test logs
  std::ostringstream oss;
  auto ostream_sink = std::make_shared<spdlog::sinks::ostream_sink_mt> (oss);
  auto logger = std::make_shared<spdlog::logger>("my_logger", ostream_sink);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::err);
  //spdlog::set_level(spdlog::level::trace);

  // create a jwt verifier
  jwt::verifier<jwt::default_clock, nlohmann_traits> 
    verifier(jwt::default_clock{});
  verifier.allow_algorithm(jwt::algorithm::hs256("secret"))
    .with_issuer("jwt-gs-test");

  minimal_game_server gs(verifier);

  std::thread server_thr(bind(&minimal_game_server::run, &gs, 9090));

  // wait for the server to start
  while(!gs.is_running()) {
    std::this_thread::sleep_for(10ms);
  }
 
  // bind a thread to process messages
  std::thread msg_process_thr(bind(&minimal_game_server::process_messages,&gs));

  // bind a thread to update all running games at regular time steps
  std::thread game_thr(bind(&minimal_game_server::update_games,&gs));

  typedef jwt::basic_claim<nlohmann_traits> claim;

  unsigned int player = 17;
  std::vector<unsigned int> players = { 17 };
  nlohmann_traits::json data = { { "players", players } };
  auto token = jwt::create<nlohmann_traits>()
    .set_issuer("jwt-gs-test")
    .set_payload_claim("id", claim(player))
    .set_payload_claim("data", claim(data))
    .sign(jwt::algorithm::hs256{"secret"});

  minimal_game_client c("ws://localhost:9090", token);
  std::thread client_thr(bind(&minimal_game_client::connect, &c));

  while(!c.is_running()) {
    std::this_thread::sleep_for(10ms);
  }

  std::this_thread::sleep_for(10ms);

  CHECK(gs.get_player_count() == 1);
  
  c.disconnect();

  while(c.is_running()) {
    std::this_thread::sleep_for(10ms);
  }

  client_thr.join();

  std::this_thread::sleep_for(10ms);

  gs.stop();

  msg_process_thr.join();
  game_thr.join();
  server_thr.join();
}
