#include <thread>
#include <functional>
#include <spdlog/spdlog.h>

#define DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

#include <jwt_game_server/game_server.hpp>
#include <json_traits/nlohmann_traits.hpp>
#include <websocketpp_configs/asio_no_logs.hpp>

#include "../tic_tac_toe_game.hpp"

using namespace std::chrono_literals;
using ttt_server = jwt_game_server::game_server<
    tic_tac_toe_game,
    jwt::default_clock, nlohmann_traits,
    asio_no_logs
  >;

int main() {
  // log level
  spdlog::set_level(spdlog::level::trace);

  // create a jwt verifier
  jwt::verifier<jwt::default_clock, nlohmann_traits> 
    verifier(jwt::default_clock{});
  verifier.allow_algorithm(jwt::algorithm::hs256("secret"))
    .with_issuer("tic_tac_toe_matchmaker");

  // create our main server to manage player connection and matchmaking
  ttt_server gs(verifier, 500ms);

  // any of the processes below can be managed by multiple threads for higher
  // performance on multi-threaded machines

  std::thread gs_server_thr{bind(&ttt_server::run, &gs, 9090, true)};

  while(!gs.is_running()) {
    std::this_thread::sleep_for(10ms);
  }

  // bind a thread to manage websocket messages
  std::thread msg_process_thr{bind(&ttt_server::process_messages,&gs)};

  // bind a thread to update all running games at regular time steps
  std::thread game_thr{bind(&ttt_server::update_games,&gs)};

  gs_server_thr.join();
  msg_process_thr.join();
  game_thr.join();
}
