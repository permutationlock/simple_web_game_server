#include <doctest/doctest.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/ostream_sink.h>

#define DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

#include <jwt_game_server/game_server.hpp>
#include <jwt_game_server/base_client.hpp>
#include <json_traits/nlohmann_traits.hpp>
#include <model_games/minimal_game.h>

#include <websocketpp_configs/asio_no_logs.hpp>
#include <websocketpp_configs/asio_client_no_logs.hpp>

#include <thread>
#include <functional>
#include <sstream>
#include <chrono>

TEST_CASE("players should be able to connect to the server with no errors") {
  using namespace std::chrono_literals;

  typedef jwt_game_server::base_client<
      asio_client_no_logs
    > minimal_game_client;

  typedef jwt_game_server::game_server<
      minimal_game,
      jwt::default_clock,
      nlohmann_traits,
      asio_no_logs
    > minimal_game_server;

  typedef jwt::basic_claim<nlohmann_traits> claim;

  // setup logging sink to track errors
  std::ostringstream oss;
  auto ostream_sink = std::make_shared<spdlog::sinks::ostream_sink_mt> (oss);
  auto logger = std::make_shared<spdlog::logger>("my_logger", ostream_sink);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::err);

  // create a jwt verifier
  jwt::verifier<jwt::default_clock, nlohmann_traits> 
    verifier(jwt::default_clock{});
  verifier.allow_algorithm(jwt::algorithm::hs256("secret"))
    .with_issuer("jwt-gs-test");

  SUBCASE("games start when players connect and die when they leave") {
    minimal_game_server gs(verifier, 10000ms);

    std::thread server_thr(bind(&minimal_game_server::run, &gs, 9090, true));

    // wait for the server to start
    while(!gs.is_running()) {
      std::this_thread::sleep_for(10ms);
    }
   
    // bind a thread to process messages
    std::thread msg_process_thr(bind(&minimal_game_server::process_messages,&gs));

    // bind a thread to update all running games at regular time steps
    std::thread game_thr(bind(&minimal_game_server::update_games,&gs));

    std::vector<minimal_game_client> clients;
    std::vector<std::thread> client_threads;

    const int PLAYER_COUNT = 200;
    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      minimal_game::player_id player = i;
      std::vector<minimal_game::player_id> players = { i };
      nlohmann_traits::json data = { { "players", players } };
      auto token = jwt::create<nlohmann_traits>()
        .set_issuer("jwt-gs-test")
        .set_payload_claim("id", claim(player))
        .set_payload_claim("data", claim(data))
        .sign(jwt::algorithm::hs256{"secret"});

      clients.push_back(minimal_game_client{"ws://localhost:9090", token});
    }

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      std::thread client_thr(bind(&minimal_game_client::connect, &(clients[i])));

      while(!clients[i].is_running()) {
        std::this_thread::sleep_for(1ms);
      }

      client_threads.push_back(std::move(client_thr));
    }

    std::this_thread::sleep_for(1000ms);

    CHECK(gs.get_player_count() == PLAYER_COUNT);
   
    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      clients[i].disconnect();
    }

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      client_threads[i].join();
    }

    std::this_thread::sleep_for(1000ms);

    CHECK(gs.get_player_count() == 0);

    gs.stop();

    msg_process_thr.join();
    game_thr.join();
    server_thr.join();

    CHECK(oss.str().empty());
  }

  SUBCASE("players should be disconnected when games end") {
    // server with fast loop that notices games are done
    minimal_game_server gs(verifier, 10ms);

    std::thread server_thr(bind(&minimal_game_server::run, &gs, 9090, true));

    // wait for the server to start
    while(!gs.is_running()) {
      std::this_thread::sleep_for(10ms);
    }
   
    // bind a thread to process messages
    std::thread msg_process_thr(bind(&minimal_game_server::process_messages,&gs));

    // bind a thread to update all running games at regular time steps
    std::thread game_thr(bind(&minimal_game_server::update_games,&gs));

    std::vector<minimal_game_client> clients;
    std::vector<std::thread> client_threads;

    const int PLAYER_COUNT = 200;
    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      minimal_game::player_id player = i;
      std::vector<minimal_game::player_id> players = { i };
      nlohmann_traits::json data = { { "players", players } };
      auto token = jwt::create<nlohmann_traits>()
        .set_issuer("jwt-gs-test")
        .set_payload_claim("id", claim(player))
        .set_payload_claim("data", claim(data))
        .sign(jwt::algorithm::hs256{"secret"});

      clients.push_back(minimal_game_client{"ws://localhost:9090", token});
    }

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      std::thread client_thr(bind(&minimal_game_client::connect, &(clients[i])));
      client_threads.push_back(std::move(client_thr));
    }

    std::this_thread::sleep_for(1000ms);

    CHECK(gs.get_player_count() == 0);
   
    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      clients[i].disconnect();
    }

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      client_threads[i].join();
    }

    std::this_thread::sleep_for(10ms);

    gs.stop();

    std::this_thread::sleep_for(10ms);

    msg_process_thr.join();
    game_thr.join();
    server_thr.join();

    CHECK(oss.str().empty());
  }
}
