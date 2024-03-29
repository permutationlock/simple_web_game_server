#include <doctest/doctest.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/ostream_sink.h>

#define DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

#include <simple_web_game_server/game_server.hpp>
#include <simple_web_game_server/client.hpp>
#include <json_traits/nlohmann_traits.hpp>

#include <websocketpp_configs/asio_no_logs.hpp>
#include <websocketpp_configs/asio_client_no_logs.hpp>

#include <thread>
#include <functional>
#include <sstream>
#include <chrono>

#include "constants.hpp"
#include "create_clients.hpp"
#include "test_game.hpp"

// create JWTs for games between the given players
// assumes that player_list.size() is divisible by GAME_SIZE
void create_game_tokens(
    std::vector<std::string>& tokens,
    const std::vector<test_game::player_traits::id::player_id> player_list,
    const std::string& secret,
    const std::string& issuer,
    std::size_t GAME_SIZE
  )
{
  using combined_id = test_game::player_traits::id;
  using player_id = combined_id::player_id;
  using session_id = combined_id::session_id;
  using claim = jwt::basic_claim<nlohmann_traits>;
  session_id sid = 0;
  
  json data = { { "matched", true } };

  // sign game tokens sorting players into games of GAME_SIZE players each
  std::vector<player_id> players;
  for(player_id pid : player_list) {
    players.push_back(pid);
    if(players.size() == GAME_SIZE) {
      for(player_id pid : players) {
        tokens.push_back(jwt::create<nlohmann_traits>()
          .set_issuer(issuer)
          .set_payload_claim("pid", claim(pid))
          .set_payload_claim("sid", claim(sid))
          .set_payload_claim("data", claim(data))
          .sign(jwt::algorithm::hs256{secret}));
      }
      players.clear();
      ++sid;
    }
  }
}

TEST_CASE("players should interact with the server with no errors") {
  using namespace std::chrono_literals;

  using game_client = simple_web_game_server::client<
      asio_client_no_logs
    >;

  using game_server = simple_web_game_server::game_server<
      test_game,
      jwt::default_clock,
      nlohmann_traits,
      asio_no_logs
    >;

  struct test_client_data {
    test_client_data() : is_connected(false) {}

    void on_open() {
      is_connected = true;
    }
    void on_close() {
      is_connected = false;
    }
    void on_message(const std::string& message) {
      messages.push_back(message);
    }

    bool is_connected;
    std::vector<std::string> messages;
  };

  using combined_id = test_game::player_traits::id;
  using player_id = combined_id::player_id;
  using session_id = combined_id::session_id;
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

  auto sign_result = [](combined_id id, const json& data){
      json temp;
      temp["pid"] = id.player;
      temp["sid"] = id.session;
      return temp.dump();
    };

  game_server gs{verifier, sign_result};
  std::thread server_thr, game_thr, msg_process_thr;
  std::size_t PLAYER_COUNT;
  std::vector<game_client> clients;
  std::vector<test_client_data> client_data_list;
  std::vector<std::thread> client_threads;
  std::vector<std::string> tokens;

  server_thr = std::thread{
      bind(&game_server::run, &gs, SERVER_PORT, true)
    };

  while(!gs.is_running()) {
    std::this_thread::sleep_for(10ms);
  }

  CHECK(oss.str() == std::string{""});

  msg_process_thr = std::thread{
      bind(&game_server::process_messages, &gs)
    };

  std::this_thread::sleep_for(100ms);

  CHECK(oss.str() == std::string{""});

  game_thr = std::thread{
      bind(&game_server::update_games, &gs, 10ms)
    };

  SUBCASE("games start when players connect") {
    std::vector<player_id> player_list = { 83, 2, 17, 339 };
    PLAYER_COUNT = player_list.size();
    const std::size_t GAME_SIZE = 2;

    CHECK(gs.get_player_count() == 0);
    CHECK(gs.get_game_count() == 0);
    
    create_game_tokens(tokens, player_list, secret, issuer, GAME_SIZE);

    create_clients<player_id, game_client, test_client_data>(
        clients, client_data_list, client_threads, tokens, uri, PLAYER_COUNT
      );

    std::this_thread::sleep_for(100ms + 10ms * PLAYER_COUNT);

    std::size_t conn_count = 0;
    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      if(client_data_list[i].is_connected) {
        conn_count++;
      }
    }

    CHECK(conn_count == PLAYER_COUNT);
    CHECK(gs.get_player_count() == PLAYER_COUNT);
    CHECK(gs.get_game_count() == PLAYER_COUNT / GAME_SIZE);
    CHECK(oss.str() == std::string{""}); 
  }

  SUBCASE("players should be disconnected when games end") {
    std::vector<player_id> player_list = { 1153, 99, 492, 35281, 74 };
    PLAYER_COUNT = player_list.size();
    const std::size_t GAME_SIZE = 1;
    
    create_game_tokens(tokens, player_list, secret, issuer, GAME_SIZE);

    create_clients<player_id, game_client, test_client_data>(
        clients, client_data_list, client_threads, tokens, uri, PLAYER_COUNT
      );

    std::this_thread::sleep_for(200ms + 20ms * PLAYER_COUNT);

    json msg = { { "type", "stop" } };
    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      clients[i].send(msg.dump());
    }

    std::this_thread::sleep_for(500ms + 20ms * PLAYER_COUNT);

    std::size_t running_count = 0;
    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      if(clients[i].is_running()) {
        running_count++;
      }
    }

    CHECK(running_count == 0);
    CHECK(gs.get_player_count() == 0);
    CHECK(gs.get_game_count() == 0);
    CHECK(oss.str() == std::string{""});
  }

  SUBCASE("only most recent client with a given token id should remain open") {
    player_id pid = 84;
    session_id sid = 192;
    PLAYER_COUNT = 3;

    nlohmann::json json_data = {
        { "matched", true }
      };
   
    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      tokens.push_back(jwt::create<nlohmann_traits>()
        .set_issuer(issuer)
        .set_payload_claim("pid", claim(pid))
        .set_payload_claim("sid", claim(sid))
        .set_payload_claim("data", claim(json_data))
        .sign(jwt::algorithm::hs256{secret}));
    }

    create_clients<player_id, game_client, test_client_data>(
        clients, client_data_list, client_threads, tokens, uri, PLAYER_COUNT, 20
      );

    std::this_thread::sleep_for(200ms + 20ms * PLAYER_COUNT);

    std::size_t conn_count = 0;
    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      if(client_data_list[i].is_connected) {
        conn_count++;
      }
    }

    CHECK(conn_count == 1);
    CHECK(gs.get_player_count() == 1);
    CHECK(gs.get_game_count() == 1);
    CHECK(client_data_list.back().is_connected == true);
    CHECK(oss.str() == std::string{""}); 
  }

  SUBCASE("players should receive a JWT when games end") {
    std::vector<player_id> player_list = { 8123, 23, 567, 90101, 32141, 7563 };
    PLAYER_COUNT = player_list.size();
    const std::size_t GAME_SIZE = 3;
    
    create_game_tokens(tokens, player_list, secret, issuer, GAME_SIZE);

    create_clients<player_id, game_client, test_client_data>(
        clients, client_data_list, client_threads, tokens, uri, PLAYER_COUNT
      );

    // long pause here since some timeouts need to resolve
    std::this_thread::sleep_for(100ms + 20ms * PLAYER_COUNT);

    json msg = { { "type", "stop" } };
    for(std::size_t i = 0; i < PLAYER_COUNT/GAME_SIZE; i++) {
      clients[i*GAME_SIZE].send(msg.dump());
    }

    std::this_thread::sleep_for(500ms + 20ms * PLAYER_COUNT);

    std::size_t running_count = 0;
    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      if(clients[i].is_running()) {
        running_count++;
      }
    }

    CHECK(running_count == 0);
    CHECK(gs.get_player_count() == 0);
    CHECK(gs.get_game_count() == 0);

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      std::string message = sign_result(
          { player_list[i], i/GAME_SIZE }, { { "valid", true } }
        );
      std::string result{""};
      if(client_data_list[i].messages.size() > 0) {
        result = client_data_list[i].messages.back();
      }

      CHECK(result == message);
      CHECK(client_data_list[i].messages.size() == 1);
    }

    CHECK(oss.str() == std::string{""});
  }

  SUBCASE("players connecting to ended session are sent the end token") {
    std::vector<player_id> player_list = { 833, 2319, 3147, 74 };
    PLAYER_COUNT = player_list.size();
    const std::size_t GAME_SIZE = 2;
    
    create_game_tokens(tokens, player_list, secret, issuer, GAME_SIZE);

    create_clients<player_id, game_client, test_client_data>(
        clients, client_data_list, client_threads, tokens, uri, PLAYER_COUNT
      );

    // long pause here because it may take 1-2ms per client to create
    std::this_thread::sleep_for(100ms + 20ms * PLAYER_COUNT);

    json msg = { { "type", "stop" } };
    for(std::size_t i = 0; i < PLAYER_COUNT/GAME_SIZE; i++) {
      clients[i*GAME_SIZE].send(msg.dump());
    }

    std::this_thread::sleep_for(500ms + 20ms * PLAYER_COUNT);

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      client_threads[i].join();
    }

    clients.clear();
    client_data_list.clear();
    client_threads.clear();

    create_clients<player_id, game_client, test_client_data>(
        clients, client_data_list, client_threads, tokens, uri, PLAYER_COUNT
      );

    std::this_thread::sleep_for(1000ms);

    std::size_t conn_count = 0;
    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      if(client_data_list[i].is_connected) {
        conn_count++;
      }
    }

    CHECK(conn_count == 0);
    CHECK(gs.get_player_count() == 0);
    CHECK(gs.get_game_count() == 0);

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      std::string message = sign_result(
          { player_list[i], i/2 },
          { { "valid", true } }
        );

      std::string result{""};
      if(client_data_list[i].messages.size() > 0) {
        result = client_data_list[i].messages.back();
      }

      CHECK(result == message);
      CHECK(client_data_list[i].messages.size() == 1);
    }

    CHECK(oss.str() == std::string{""}); 
  }

  // end of test cleanup

  for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
    try {
      clients[i].disconnect();
    } catch(game_client::client_error& e) {}
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
