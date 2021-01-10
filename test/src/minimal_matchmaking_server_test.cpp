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
      minimal_matchmaker,
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

  using player_id = minimal_matchmaker::player_id;
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

  auto sign_game = [](player_id id, const json& data){
      json temp;
      temp["id"] = id;
      temp["data"] = data;
      return temp.dump();
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

    std::set<player_id> player_list;
    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      player_list.insert(i);
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

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      json game_data;
      try {
        nlohmann_traits::parse(game_data, client_data_list[i].last_message);
      } catch(std::exception& e) {}

      player_id id;
      try {
        id = game_data["id"].get<player_id>();
      } catch(std::exception& e) {}

      CHECK(id == i);

      std::set<player_id> game_player_list;
      try {
        std::vector<player_id> pl{
            game_data["data"]["players"].get<std::vector<player_id> >()
          };

        for(player_id id : pl) {
          game_player_list.insert(id);
        }
      } catch(std::exception& e) {}

      CHECK(game_player_list == player_list);
    }

    CHECK(oss.str() == std::string{""});
  }

  SUBCASE("matchmaker should match an even number players all into games") {
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

    std::map<player_id, player_id> opponent_map;

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      player_id id;
      std::vector<player_id> player_list;
      try {
        json game_data;
        nlohmann_traits::parse(game_data, client_data_list[i].last_message);
        id = game_data["id"].get<player_id>();
        player_list =
          game_data["data"]["players"].get<std::vector<player_id> >();
      } catch(std::exception& e) {}

      CHECK(id == i);
      CHECK(player_list.size() == 2);

      if(player_list.size() == 2) {
        player_id p1 = player_list.front(); 
        player_id p2 = player_list.back(); 
        if(opponent_map.count(p1) == 0) {
          opponent_map[p1] = p2;
        } else {
          CHECK(opponent_map[p1] == p2);
        }
        if(opponent_map.count(p2) == 0) {
          opponent_map[p2] = p1;
        } else {
          CHECK(opponent_map[p2] == p1);
        }
      }
    }

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      CHECK(opponent_map.count(i) > 0);
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
