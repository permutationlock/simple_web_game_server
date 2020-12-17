#include <thread>
#include <functional>
#include <spdlog/spdlog.h>

#define DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

#include <jwt_game_server/matchmaking_server.hpp>

#include "../json_traits.hpp"
#include "../tic_tac_toe_game.hpp"

typedef jwt_game_server::matchmaking_server<
    tic_tac_toe_matchmaking_data,
    jwt::default_clock,
    nlohmann_traits
  > mm_server;

int main() {
  // log level
  spdlog::set_level(spdlog::level::trace);

  // create a jwt verifier
  jwt::verifier<jwt::default_clock, nlohmann_traits> 
    verifier(jwt::default_clock{});
  verifier.allow_algorithm(jwt::algorithm::hs256("passwd"))
    .with_issuer("tictactoe");

  // create our main server to manage player connection and matchmaking
  mm_server gs(verifier);

  // any of the processes below can be managed by multiple threads for higher
  // performance on multi-threaded machines

  // bind a thread to manage websocket messages
  std::thread mm_thr(bind(&mm_server::match_players, &gs));

  gs.run(9080);
}
