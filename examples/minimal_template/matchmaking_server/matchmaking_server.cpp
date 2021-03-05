#include <thread>
#include <chrono>
#include <functional>
#include <spdlog/spdlog.h>

#define DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

#include <simple_web_game_server/matchmaking_server.hpp>
#include <json_traits/nlohmann_traits.hpp>
#include <websocketpp_configs/asio_no_logs.hpp>

#include "../minimal_game.hpp"

using namespace std::chrono_literals;
using claim = jwt::basic_claim<nlohmann_traits>;
using combined_id = minimal_matchmaker::player_traits::id;

using matchmaking_server = simple_web_game_server::matchmaking_server<
    minimal_matchmaker,
    jwt::default_clock, nlohmann_traits,
    asio_no_logs
  >;

using context_ptr =
  websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context>;

int main() {
  // log level
  spdlog::set_level(spdlog::level::trace);

  const std::string secret = "secret";

  // create a jwt verifier
  jwt::verifier<jwt::default_clock, nlohmann_traits> 
    verifier(jwt::default_clock{});
  verifier.allow_algorithm(jwt::algorithm::hs256(secret))
    .with_issuer("auth_server");

  // create a function to sign game tokens
  auto sign_game = [=](combined_id id, const json& data){ 
      std::string token = jwt::create<nlohmann_traits>()
        .set_issuer("matchmaking_server")
        .set_payload_claim("pid", claim(id.player))
        .set_payload_claim("sid", claim(id.session))
        .set_payload_claim("data", claim(data))
        .sign(jwt::algorithm::hs256{secret});
      json temp = { { "type", "token" }, { "token", token } };
      return temp.dump();
    };

  // create our main server to manage player connection and matchmaking
  matchmaking_server mms{verifier, sign_game, 60s};

  std::thread mms_server_thr{bind(&matchmaking_server::run, &mms, 9091, true)};

  while(!mms.is_running()) {
    std::this_thread::sleep_for(10ms);
  }

  std::thread msg_process_thr{
      bind(&matchmaking_server::process_messages, &mms)
    };

  std::thread match_thr{bind(&matchmaking_server::match_players, &mms, 10ms)};

  mms_server_thr.join();
  msg_process_thr.join();
  match_thr.join();
}
