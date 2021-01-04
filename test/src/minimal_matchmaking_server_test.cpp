#include <doctest/doctest.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/ostream_sink.h>

#define DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

#include <jwt_game_server/matchmaking_server.hpp>
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

  using minimal_client = jwt_game_server::client<
      asio_client_no_logs
    >;

  using minimal_matchmaking_server = jwt_game_server::matchmaking_server<
      minimal_matchmaking_data,
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
      last_message = message;
    }

    bool is_connected;
    std::string last_message;
  };

  using player_id = minimal_matchmaking_data::player_id;
  using json = nlohmann::json;
  using claim = jwt::basic_claim<nlohmann_traits>;

  // setup logging sink to track errors
  std::ostringstream oss;
  auto ostream_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
  auto logger = std::make_shared<spdlog::logger>("my_logger", ostream_sink);
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::err);

  // create a jwt verifier
  const std::string secret = "secret";
  const std::string issuer = "jwt-gs-text";
  jwt::verifier<jwt::default_clock, nlohmann_traits> 
    verifier(jwt::default_clock{});
  verifier.allow_algorithm(jwt::algorithm::hs256(secret))
    .with_issuer(issuer);

  std::string uri = std::string{"ws://localhost:"}
    + std::to_string(SERVER_PORT);

  auto sign_game = [=](player_id id, const json& data){ 
      return jwt::create<nlohmann_traits>()
        .set_issuer(issuer)
        .set_payload_claim("id", claim(id))
        .set_payload_claim("data", claim(data))
        .sign(jwt::algorithm::hs256{secret});
    };

  minimal_matchmaking_server mms{
      verifier, 
      sign_game,
      100ms
    };
  std::thread server_thr, match_thr, msg_process_thr;

  server_thr = std::thread{
      bind(&minimal_matchmaking_server::run, &mms, SERVER_PORT, true)
    };

  while(!mms.is_running()) {
    std::this_thread::sleep_for(10ms);
  }

  CHECK(oss.str() == std::string{""});
 
  msg_process_thr = std::thread{
      bind(&minimal_matchmaking_server::process_messages,&mms)
    };

  match_thr = std::thread{
      bind(&minimal_matchmaking_server::match_players, &mms)
    };

  std::this_thread::sleep_for(100ms);

  CHECK(oss.str() == std::string{""});

  std::size_t PLAYER_COUNT;
  std::vector<minimal_client> clients;
  std::vector<test_client_data> client_data_list;
  std::vector<std::thread> client_threads;
  std::vector<std::string> tokens;

  SUBCASE("matchmaker should not match until two players connect") {
    PLAYER_COUNT = 1;

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      player_id player = i;
      nlohmann::json json_data{json::value_t::object};

      tokens.push_back(jwt::create<nlohmann_traits>()
        .set_issuer(issuer)
        .set_payload_claim("id", claim(player))
        .set_payload_claim("data", claim(json_data))
        .sign(jwt::algorithm::hs256{secret}));
    }

    create_clients<player_id, minimal_client, test_client_data>(
        clients, client_data_list, client_threads, tokens, uri, PLAYER_COUNT
      );

    std::this_thread::sleep_for(100ms);

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      CHECK(client_data_list[i].last_message == "");
    }

    CHECK(mms.get_player_count() == PLAYER_COUNT);
    CHECK(oss.str() == std::string{""});
  }

  SUBCASE("matchmaker should match two players together") {
    PLAYER_COUNT = 2;

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      player_id player = i;
      nlohmann::json json_data{json::value_t::object};

      tokens.push_back(jwt::create<nlohmann_traits>()
        .set_issuer(issuer)
        .set_payload_claim("id", claim(player))
        .set_payload_claim("data", claim(json_data))
        .sign(jwt::algorithm::hs256{secret}));
    }

    create_clients<player_id, minimal_client, test_client_data>(
        clients, client_data_list, client_threads, tokens, uri, PLAYER_COUNT
      );

    std::this_thread::sleep_for(1000ms);

    json game_data{ { "players" , std::vector<player_id>{0, 1} } };
    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      std::string message = sign_game(i, game_data);
      CHECK(client_data_list[i].last_message == message);
    }

    CHECK(oss.str() == std::string{""});
  }

  SUBCASE("matchmaker should match all players together") {
    PLAYER_COUNT = 50;

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      player_id player = i;
      nlohmann::json json_data{json::value_t::object};

      tokens.push_back(jwt::create<nlohmann_traits>()
        .set_issuer(issuer)
        .set_payload_claim("id", claim(player))
        .set_payload_claim("data", claim(json_data))
        .sign(jwt::algorithm::hs256{secret}));
    }

    create_clients<player_id, minimal_client, test_client_data>(
        clients, client_data_list, client_threads, tokens, uri, PLAYER_COUNT
      );

    std::this_thread::sleep_for(1000ms);

    std::map<player_id, std::vector<player_id> > player_vec_map;
    std::vector<player_id> players;
    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      players.push_back(i);
      if(players.size() == 2) {
        for(player_id id : players) {
          player_vec_map[id] = players;
        }
        players.clear();
      }
    }

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      json game_data{ { "players" , player_vec_map[i] } };
      std::string message = sign_game(i, game_data);
      CHECK(client_data_list[i].last_message == message);
    }

    CHECK(oss.str() == std::string{""});
  }

  SUBCASE("matchmaker should leave a player unmatched if uneven player count") {
    PLAYER_COUNT = 101;

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      player_id player = i;
      nlohmann::json json_data{json::value_t::object};

      tokens.push_back(jwt::create<nlohmann_traits>()
        .set_issuer(issuer)
        .set_payload_claim("id", claim(player))
        .set_payload_claim("data", claim(json_data))
        .sign(jwt::algorithm::hs256{secret}));
    }

    create_clients<player_id, minimal_client, test_client_data>(
        clients, client_data_list, client_threads, tokens, uri, PLAYER_COUNT
      );

    std::this_thread::sleep_for(1000ms);

    CHECK(mms.get_player_count() == 1);
    CHECK(oss.str() == std::string{""});
  }

  // end of test cleanup

  for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
    clients[i].disconnect();
  }

  for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
    client_threads[i].join();
  }

  std::this_thread::sleep_for(100ms);

  CHECK(mms.get_player_count() == 0);
  CHECK(oss.str() == std::string{""});

  mms.stop();

  std::this_thread::sleep_for(100ms);
  CHECK(oss.str() == std::string{""});

  msg_process_thr.join();
  match_thr.join();
  server_thr.join();
}
