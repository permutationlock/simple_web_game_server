#include <thread>
#include <functional>
#include <spdlog/spdlog.h>

#include "tic_tac_toe_server.hpp"

using json = nlohmann::json;

typedef main_server<tic_tac_toe_game> ttt_server;

int main() {
    // log level
    spdlog::set_level(spdlog::level::debug);

    // create our main server to manage player connection and matchmaking
    ttt_server gs;

    // any of the processes below can be managed by multiple threads for higher
    // performance on multi-threaded machines

    // bind a thread to manage websocket messages
    std::thread msg_process_thr(bind(&ttt_server::process_messages,&gs));

    // bind a thread to update all running games at regular time steps
    std::thread game_thr(bind(&ttt_server::update_games,&gs));
    
    gs.run(9090);
}
