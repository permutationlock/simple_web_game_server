#include <thread>
#include <functional>

#include "game_server.hpp"

using json = nlohmann::json;

int main() {
    // create our main server to manage player connection and matchmaking
    main_server gs;

    // any of the processes below can be managed by multiple threads for higher
    // performance on multi-threaded machines

    // bind a thread to manage websocket messages
    std::thread msg_process_thr(bind(&main_server::process_messages,&gs));

    // bind a thread to matchmake players into games
    std::thread matchmaking_thr(bind(&main_server::match_players,&gs));

    // bind a thread to update all running games at regular time steps
    std::thread game_thr(bind(&main_server::update_games,&gs));
    
    gs.run(9090);
}
