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

// create JWTs matchmaking auth tokens for the given players
void create_matchmaker_tokens(
    std::vector<std::string>& tokens,
    const std::vector<minimal_game::player_traits::id> player_list,
    const std::string& secret,
    const std::string& issuer
  )
{
  using json = nlohmann::json;
  using claim = jwt::basic_claim<nlohmann_traits>;

  for(auto& id : player_list) {
    tokens.push_back(jwt::create<nlohmann_traits>()
        .set_issuer(issuer)
        .set_payload_claim("pid", claim(id.player))
        .set_payload_claim("sid", claim(id.session))
        .set_payload_claim("data", claim(json::value_t::object))
        .sign(jwt::algorithm::hs256{secret})
      );
  }
}

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

  using combined_id = minimal_game::player_traits::id;
  using player_id = combined_id::player_id;
  using session_id = combined_id::session_id;
  using json = nlohmann::json;

  // setup logging sink to track errors
  std::ostringstream oss;
  //auto ostream_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(oss);
  //auto logger = std::make_shared<spdlog::logger>("my_logger", ostream_sink);
  //spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::trace);

  // create a jwt verifier
  const std::string secret = "secret";
  const std::string issuer = "jwt-gs-text";
  jwt::verifier<jwt::default_clock, nlohmann_traits> 
    verifier(jwt::default_clock{});
  verifier.allow_algorithm(jwt::algorithm::hs256(secret))
    .with_issuer(issuer);

  std::string uri = std::string{"ws://localhost:"}
    + std::to_string(SERVER_PORT);

  auto sign_game = [](const combined_id& id, const json& data){
      json temp;
      temp["pid"] = id.player;
      temp["sid"] = id.session;
      temp["data"] = data;
      return temp.dump();
    };

  minimal_matchmaking_server mms{
      verifier, 
      sign_game
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
      bind(&minimal_matchmaking_server::match_players, &mms, 100ms)
    };

  std::this_thread::sleep_for(100ms);

  CHECK(oss.str() == std::string{""});

  std::size_t PLAYER_COUNT;
  std::vector<minimal_client> clients;
  std::vector<test_client_data> client_data_list;
  std::vector<std::thread> client_threads;
  std::vector<std::string> tokens;

  SUBCASE("matchmaker should not match until two players connect") {
    std::vector<combined_id> player_list{ { 82, 0 } };
    PLAYER_COUNT = player_list.size();

    create_matchmaker_tokens(tokens, player_list, secret, issuer);

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
    std::vector<combined_id> player_list{ { 31, 4}, { 82123, 5 } };
    PLAYER_COUNT = player_list.size();

    create_matchmaker_tokens(tokens, player_list, secret, issuer);

    create_clients<player_id, minimal_client, test_client_data>(
        clients, client_data_list, client_threads, tokens, uri, PLAYER_COUNT
      );

    std::this_thread::sleep_for(1000ms);

    std::map<player_id, session_id> session_map;
    std::map<session_id, std::set<player_id> > pl_map;

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      player_id pid;
      session_id sid;
      try {
        json game_data;
        nlohmann_traits::parse(game_data, client_data_list[i].last_message);
        pid = game_data["pid"].get<player_id>();
        sid = game_data["sid"].get<session_id>();
        session_map[pid] = sid;
        pl_map[sid].insert(pid);
      } catch(std::exception& e) {
        spdlog::trace("AHHHHHHHHH: ", client_data_list[i].last_message);
      }

      CHECK(pid == player_list[i].player);
    }

    for(const combined_id& id : player_list) {
      CHECK(session_map.count(id.player) > 0);
    }

    std::size_t total_player_count = 0;
    for(auto& sid_pl_pair : pl_map) {
      CHECK(sid_pl_pair.second.size() == 2);
      total_player_count += sid_pl_pair.second.size();
    }

    CHECK(total_player_count == PLAYER_COUNT);

    CHECK(oss.str() == std::string{""});
  }

  SUBCASE("matchmaker should match an even number players all into games") {
    std::vector<combined_id> player_list{
        { 9813, 1 }, { 23, 2 }, { 85231, 3 }, { 753, 4 }
      };
    PLAYER_COUNT = player_list.size();

    create_matchmaker_tokens(tokens, player_list, secret, issuer);

    create_clients<player_id, minimal_client, test_client_data>(
        clients, client_data_list, client_threads, tokens, uri, PLAYER_COUNT
      );

    std::this_thread::sleep_for(1000ms);

    std::map<player_id, session_id> session_map;
    std::map<session_id, std::set<player_id> > pl_map;

    for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
      player_id pid;
      session_id sid;
      try {
        json game_data;
        nlohmann_traits::parse(game_data, client_data_list[i].last_message);
        pid = game_data["pid"].get<player_id>();
        sid = game_data["sid"].get<session_id>();
        session_map[pid] = sid;
        pl_map[sid].insert(pid);
      } catch(std::exception& e) {
      }

      CHECK(pid == player_list[i].player);
    }

    for(const combined_id& id : player_list) {
      CHECK(session_map.count(id.player) > 0);
    }

    std::size_t total_player_count = 0;
    for(auto& sid_pl_pair : pl_map) {
      CHECK(sid_pl_pair.second.size() == 2);
      total_player_count += sid_pl_pair.second.size();
    }

    CHECK(total_player_count == PLAYER_COUNT);

    CHECK(oss.str() == std::string{""});
  }

  SUBCASE("matchmaker should leave a player unmatched if uneven player count") {
    std::vector<combined_id> player_list{ { 9, 0 }, { 65, 1 }, { 41, 2 },
      { 701, 3 }, { 83, 4 } };
    PLAYER_COUNT = player_list.size();

    create_matchmaker_tokens(tokens, player_list, secret, issuer);

    create_clients<player_id, minimal_client, test_client_data>(
        clients, client_data_list, client_threads, tokens, uri, PLAYER_COUNT
      );

    std::this_thread::sleep_for(1000ms);

    CHECK(mms.get_player_count() == 1);
    CHECK(oss.str() == std::string{""});
  }

  SUBCASE("only most recent client with given id should be matched") {
    std::vector<combined_id> player_list{ { 53, 10 }, { 53, 10 }, { 3, 11 } };
    PLAYER_COUNT = player_list.size();

    create_matchmaker_tokens(tokens, player_list, secret, issuer);

    create_clients<player_id, minimal_client, test_client_data>(
        clients, client_data_list, client_threads, tokens, uri, PLAYER_COUNT, 100
      );

    std::this_thread::sleep_for(1000ms);

    CHECK(mms.get_player_count() == 0);
    CHECK(client_data_list[0].last_message == "");
    CHECK(client_data_list[1].last_message != "");
    CHECK(oss.str() == std::string{""});
  }

  // end of test cleanup

  for(std::size_t i = 0; i < PLAYER_COUNT; i++) {
    try {
      clients[i].disconnect();
    } catch(minimal_client::client_error& e) {}
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
