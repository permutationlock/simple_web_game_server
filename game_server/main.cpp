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
    broadcast_server game_server;

    std::thread t(bind(&broadcast_server::process_messages,&game_server));
    
    game_server.run(9090);
}
