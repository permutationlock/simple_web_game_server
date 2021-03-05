#include <thread>
#include <functional>
#include <spdlog/spdlog.h>

#define DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

#include <simple_web_game_server/game_server.hpp>
#include <json_traits/nlohmann_traits.hpp>
#include <websocketpp_configs/asio_no_logs.hpp>

#include "../minimal_game.hpp"

using namespace std::chrono_literals;
using game_server = simple_web_game_server::game_server<
    minimal_game,
    jwt::default_clock, nlohmann_traits,
    asio_no_logs
  >;

using claim = jwt::basic_claim<nlohmann_traits>;
using combined_id = minimal_game::player_traits::id;

using context_ptr =
  websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context>;

const long PORT = 9090;

int main() {
  // log level
  spdlog::set_level(spdlog::level::trace);

  // create a jwt verifier
  jwt::verifier<jwt::default_clock, nlohmann_traits> 
    verifier(jwt::default_clock{});
  verifier.allow_algorithm(jwt::algorithm::hs256("secret"))
    .with_issuer("matchmaking_server");

  // create a function to sign game result tokens
  auto sign_game = [](const combined_id& id, const json& data){ 
      std::string token = jwt::create<nlohmann_traits>()
        .set_issuer("game_server")
        .set_payload_claim("pid", claim(id.player))
        .set_payload_claim("sid", claim(id.session))
        .set_payload_claim("data", claim(data))
        .sign(jwt::algorithm::hs256{"secret"});
      json temp = { { "type", "token" }, { "token", token } };
      return temp.dump();
    };

  // create our main server to manage player connection and matchmaking
  game_server gs(verifier, sign_game, 60s);

  // bind a thread to run the websocket server
  // may be run by multiple threads if desired
  std::thread gs_server_thr{bind(&game_server::run, &gs, PORT, true)};

  while(!gs.is_running()) {
    std::this_thread::sleep_for(10ms);
  }

  // bind a thread to manage websocket messages
  // may be run by multiple threads if desired
  std::thread msg_process_thr{bind(&game_server::process_messages,&gs)};

  // bind a thread to update all running games at regular time steps
  std::thread game_thr{bind(&game_server::update_games, &gs, 16ms)};

  gs_server_thr.join();
  msg_process_thr.join();
  game_thr.join();
}
