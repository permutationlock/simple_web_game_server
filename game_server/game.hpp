#ifndef CATACRAWL_GAME_HPP
#define CATACRAWL_GAME_HPP

#include "player.hpp"
#include "game_server.hpp"

class game {
    public:
        game(const std::vector<player> & ps, server* s);
        void update();
    private:
        std::vector<player> players;
        server* ws_server;
};

game::game(const std::vector<player> & ps, server* s) :
    players(ps), ws_server(s) {}

void game::update() {
    json update_json;
    update_json["type"] = "PLAYER_UPDATE";
    auto data_json = update_json["data"];
    for(auto & player : players) {
        json player_json;
        player_json["id"] = player.get_id();
        player_json["x"] = player.get_x();
        player_json["y"] = player.get_y();
        data_json.push_back(player_json);
    }

    for(auto & player : players) { 
        try {
            ws_server->send(
                    player.get_connection(),
                    update_json.dump(4),
                    websocketpp::frame::opcode::text
                );
        } catch (websocketpp::exception const & e) {
            std::cout << "Echo failed because: "
                      << "(" << e.what() << ")" << std::endl;
        }
    }
}

#endif // CATACRAWL_GAME_HPP
