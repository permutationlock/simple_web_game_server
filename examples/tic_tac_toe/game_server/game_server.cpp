#include <thread>
#include <functional>
#include <spdlog/spdlog.h>

#define DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

#include <jwt_game_server/game_server.hpp>
#include <json_traits/nlohmann_traits.hpp>

#include "../tic_tac_toe_game.hpp"

typedef jwt_game_server::
  game_server<tic_tac_toe_game, jwt::default_clock, nlohmann_traits>
  ttt_server;

int main() {
  // log level
  spdlog::set_level(spdlog::level::trace);

  // create a jwt verifier
  jwt::verifier<jwt::default_clock, nlohmann_traits> 
    verifier(jwt::default_clock{});
  verifier.allow_algorithm(jwt::algorithm::hs256("passwd"))
    .with_issuer("krynth");

  // create our main server to manage player connection and matchmaking
  ttt_server gs(verifier);

  // any of the processes below can be managed by multiple threads for higher
  // performance on multi-threaded machines

  // bind a thread to manage websocket messages
  std::thread msg_process_thr(bind(&ttt_server::process_messages,&gs));

  // bind a thread to update all running games at regular time steps
  std::thread game_thr(bind(&ttt_server::update_games,&gs));
  
  gs.run(9090);
}
