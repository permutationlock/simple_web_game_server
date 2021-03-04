#include <thread>
#include <functional>
#include <spdlog/spdlog.h>

#define DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

#include <simple_web_game_server/game_server.hpp>
#include <json_traits/nlohmann_traits.hpp>
#include <websocketpp_configs/asio_no_logs.hpp>

#include "chat_game.hpp"

using namespace std::chrono_literals;
using ttt_server = simple_web_game_server::game_server<
    chat_game,
    jwt::default_clock, nlohmann_traits,
    asio_no_logs
  >;

using claim = jwt::basic_claim<nlohmann_traits>;
using combined_id = chat_player_traits::id;

using context_ptr =
  websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context>;

int main() {
  // log level
  spdlog::set_level(spdlog::level::trace);

  // create a jwt verifier
  jwt::verifier<jwt::default_clock, nlohmann_traits> 
    verifier(jwt::default_clock{});
  verifier.allow_algorithm(jwt::algorithm::hs256("secret"))
    .with_issuer("chat_auth");

  // create a function to sign game result tokens
  auto sign_game = [](const combined_id& id, const json& data){ 
      return std::string("room closed");
    };

  // create our main server to manage player connection and matchmaking
  ttt_server gs(verifier, sign_game, 1s);

  // run the server to listen and receive messages
  std::thread gs_server_thr{bind(&ttt_server::run, &gs, 9090, true)};

  while(!gs.is_running()) {
    std::this_thread::sleep_for(10ms);
  }

  // bind a thread to manage websocket server actions
  std::thread msg_process_thr{bind(&ttt_server::process_messages,&gs)};

  // bind a thread to update all running games at regular time steps
  std::thread game_thr{bind(&ttt_server::update_games, &gs, 100ms)};

  gs_server_thr.join();
  msg_process_thr.join();
  game_thr.join();
}
