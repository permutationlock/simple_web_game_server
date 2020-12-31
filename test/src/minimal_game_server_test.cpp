#include <doctest/doctest.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/ostream_sink.h>

#define DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

#include <jwt_game_server/game_server.hpp>
#include <jwt_game_server/client.hpp>
#include <json_traits/nlohmann_traits.hpp>
#include <model_games/minimal_game.hpp>

#include <websocketpp_configs/asio_no_logs.hpp>
#include <websocketpp_configs/asio_client_no_logs.hpp>

#include <thread>
#include <functional>
#include <sstream>
#include <chrono>

#include "constants.hpp"
#include "create_clients.hpp"

TEST_CASE("players should interact with the server with no errors") {
  using namespace std::chrono_literals;

  using minimal_game_client = jwt_game_server::client<
      asio_client_no_logs
    >;

  using minimal_game_server = jwt_game_server::game_server<
      minimal_game,
      jwt::default_clock,
      nlohmann_traits,
      asio_no_logs
    >;

  struct test_client_data {
    void on_open() {}
    void on_close() {}
    void on_message(const std::string& message) {
      last_message = message;
    }

    std::string last_message;
  };

  using player_id = minimal_game::player_id;
  using claim = jwt::basic_claim<nlohmann_traits>;

  // setup logging sink to track errors
  std::ostringstream oss;
  auto ostream_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
  auto logger = std::make_shared<spdlog::logger>("my_logger", ostream_sink);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::err);

  // create a jwt verifier
  std::string secret = "secret";
  std::string issuer = "jwt-gs-text";
  jwt::verifier<jwt::default_clock, nlohmann_traits> 
    verifier(jwt::default_clock{});
  verifier.allow_algorithm(jwt::algorithm::hs256(secret))
    .with_issuer(issuer);

  std::string uri = std::string{"ws://localhost:"}
    + std::to_string(SERVER_PORT);

  minimal_game_server gs{verifier};
  std::thread server_thr, game_thr, msg_process_thr;
  std::size_t PLAYER_COUNT;
  std::vector<minimal_game_client> clients;
  std::vector<test_client_data> client_data_list;
  std::vector<std::thread> client_threads;
  std::vector<std::string> tokens;

  SUBCASE("games start when players connect and die when they leave") {
    gs.set_timestep(10000ms);

    server_thr = std::thread{
        bind(&minimal_game_server::run, &gs, SERVER_PORT, true)
      };

    while(!gs.is_running()) {
      std::this_thread::sleep_for(10ms);
    }

    CHECK(oss.str() == std::string{""});
   
    msg_process_thr = std::thread{
        bind(&minimal_game_server::process_messages,&gs)
      };

    std::this_thread::sleep_for(100ms);

    CHECK(oss.str() == std::string{""});

    game_thr = std::thread{bind(&minimal_game_server::update_games,&gs)};

    PLAYER_COUNT = 200;

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      player_id player = i;
      std::vector<player_id> players = { player };
      nlohmann::json json_data = { { "players", players } };

      tokens.push_back(jwt::create<nlohmann_traits>()
        .set_issuer(issuer)
        .set_payload_claim("id", claim(player))
        .set_payload_claim("data", claim(json_data))
        .sign(jwt::algorithm::hs256{secret}));
    }

    create_clients<player_id, minimal_game_client, test_client_data>(
        clients, client_data_list, client_threads, tokens, uri, PLAYER_COUNT
      );

    std::this_thread::sleep_for(100ms);

    CHECK(gs.get_player_count() == PLAYER_COUNT);
    CHECK(oss.str() == std::string{""}); 
  }

  SUBCASE("players should be disconnected when games end") {
    // game loop set to 10ms so games timeout during the 1s pause
    gs.set_timestep(10ms);

    server_thr = std::thread{
        bind(&minimal_game_server::run, &gs, SERVER_PORT, true)
      };

    while(!gs.is_running()) {
      std::this_thread::sleep_for(10ms);
    }

    CHECK(oss.str() == std::string{""});
   
    msg_process_thr = std::thread{
        bind(&minimal_game_server::process_messages,&gs)
      };

    std::this_thread::sleep_for(100ms);

    CHECK(oss.str() == std::string{""});

    game_thr = std::thread{bind(&minimal_game_server::update_games,&gs)};

    PLAYER_COUNT = 200;

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      player_id player = i;
      std::vector<player_id> players = { player };
      nlohmann::json json_data = { { "players", players } };

      tokens.push_back(jwt::create<nlohmann_traits>()
        .set_issuer(issuer)
        .set_payload_claim("id", claim(player))
        .set_payload_claim("data", claim(json_data))
        .sign(jwt::algorithm::hs256{secret}));
    }

    create_clients<player_id, minimal_game_client, test_client_data>(
        clients, client_data_list, client_threads, tokens, uri, PLAYER_COUNT
      );

    // long pause here since some timeouts need to resolve
    std::this_thread::sleep_for(1000ms);

    CHECK(gs.get_player_count() == 0);
    CHECK(oss.str() == std::string{""});

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      CHECK(clients[i].is_running() == false);
    }
  }

  SUBCASE("player messages should be echoed back") {
    gs.set_timestep(10000ms);

    server_thr = std::thread{
        bind(&minimal_game_server::run, &gs, SERVER_PORT, true)
      };

    while(!gs.is_running()) {
      std::this_thread::sleep_for(10ms);
    }

    CHECK(oss.str() == std::string{""});
   
    msg_process_thr = std::thread{
        bind(&minimal_game_server::process_messages,&gs)
      };

    std::this_thread::sleep_for(100ms);

    CHECK(oss.str() == std::string{""});

    game_thr = std::thread{bind(&minimal_game_server::update_games,&gs)};

    PLAYER_COUNT = 10;

    // place each player into a one player game
    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      player_id player = i;
      std::vector<player_id> players = { player };
      nlohmann::json json_data = { { "players", players } };

      tokens.push_back(jwt::create<nlohmann_traits>()
        .set_issuer(issuer)
        .set_payload_claim("id", claim(player))
        .set_payload_claim("data", claim(json_data))
        .sign(jwt::algorithm::hs256{secret}));
    }

    create_clients<player_id, minimal_game_client, test_client_data>(
        clients, client_data_list, client_threads, tokens, uri, PLAYER_COUNT
      );

    std::this_thread::sleep_for(100ms);

    CHECK(oss.str() == std::string{""});

    std::string message{"{\"text\":\"hello in there\"}"};

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      clients[i].send(message);
    }

    std::this_thread::sleep_for(100ms);

    CHECK(oss.str() == std::string{""});

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      CHECK(client_data_list[i].last_message == message);
    }
  }

  SUBCASE("player messages should be echoed back to all players in the game") {
    gs.set_timestep(10000ms);

    server_thr = std::thread{
        bind(&minimal_game_server::run, &gs, SERVER_PORT, true)
      };

    while(!gs.is_running()) {
      std::this_thread::sleep_for(10ms);
    }

    CHECK(oss.str() == std::string{""});
   
    msg_process_thr = std::thread{
        bind(&minimal_game_server::process_messages,&gs)
      };

    std::this_thread::sleep_for(100ms);

    CHECK(oss.str() == std::string{""});

    game_thr = std::thread{bind(&minimal_game_server::update_games,&gs)};

    PLAYER_COUNT = 10;
    const std::size_t GAME_SIZE = 2;

    // sign game tokens sorting players into games of GAME_SIZE players each
    std::vector<player_id> players;
    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      players.push_back(i);
      if(players.size() == GAME_SIZE) {
        for(player_id player : players) {
          nlohmann::json json_data = { { "players", players } };

          tokens.push_back(jwt::create<nlohmann_traits>()
            .set_issuer(issuer)
            .set_payload_claim("id", claim(player))
            .set_payload_claim("data", claim(json_data))
            .sign(jwt::algorithm::hs256{secret}));
        }
        players.clear();
      }
    }

    create_clients<player_id, minimal_game_client, test_client_data>(
        clients, client_data_list, client_threads, tokens, uri, PLAYER_COUNT
      );

    std::this_thread::sleep_for(100ms);

    CHECK(oss.str() == std::string{""});

    std::string message{"{\"text\":\"hello in there\"}"};

    // have one player in each game send a message
    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      if(i % GAME_SIZE == 0) {
        clients[i].send(message);
      }
    }

    std::this_thread::sleep_for(100ms);

    CHECK(oss.str() == std::string{""});

    // check all players received the echo
    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      CHECK(client_data_list[i].last_message == message);
    }
  }

  // end of test cleanup

  for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
    clients[i].disconnect();
  }

  for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
    client_threads[i].join();
  }

  std::this_thread::sleep_for(100ms);

  CHECK(gs.get_player_count() == 0);
  CHECK(oss.str() == std::string{""});

  gs.stop();

  std::this_thread::sleep_for(100ms);
  CHECK(oss.str() == std::string{""});

  msg_process_thr.join();
  game_thr.join();
  server_thr.join();
}
