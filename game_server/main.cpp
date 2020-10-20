#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <nlohmann/json.hpp>

#include <iostream>

#include "player.hpp"
#include "game_server.hpp"
#include <thread>
#include <functional>

using json = nlohmann::json;

int main() {
    // Create a server endpoint
    main_server gs;

    std::thread msg_process_thr(bind(&main_server::process_messages,&gs));
    std::thread matchmaking_thr(bind(&main_server::match_players,&gs));
    std::thread game_thr(bind(&main_server::update_games,&gs));
    
    gs.run(9090);
}
