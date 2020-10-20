#ifndef CATACRAWL_GAME_SERVER_HPP
#define CATACRAWL_GAME_SERVER_HPP

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <iostream>
#include <set>
#include <mutex>
#include <condition_variable>
#include <chrono>

#include "game.hpp"

using namespace std::chrono_literals;

// Minimum time between game updates
constexpr std::chrono::nanoseconds timestep(1000ms);

typedef websocketpp::server<websocketpp::config::asio> server;

using websocketpp::connection_hdl;

using std::bind;
using std::placeholders::_1;
using std::placeholders::_2;

using std::shared_ptr;
using std::make_shared;
using std::mutex;
using std::lock_guard;
using std::unique_lock;
using std::condition_variable;

enum action_type {
    SUBSCRIBE,
    UNSUBSCRIBE,
    MESSAGE
};

struct action {
    action(action_type t, connection_hdl h) : type(t), hdl(h) {}
    action(action_type t, connection_hdl h, server::message_ptr m)
      : type(t), hdl(h), msg(m) {}

    action_type type;
    websocketpp::connection_hdl hdl;
    server::message_ptr msg;
};

class game_server {
public:
    game_server(server* s) : m_server_ptr(s), m_time(0)  {}

    void add_player(player_id id, connection_hdl hdl) {
        lock_guard<mutex> guard(m_game_lock);
        m_player_connection[id] = hdl;
        m_player_status[id] = true;
    }

    void reconnect(player_id id, connection_hdl hdl) {
        lock_guard<mutex> guard(m_game_lock);
        m_player_connection[id] = hdl;
        m_player_status[id] = true;
    }

    void remove_player(player_id id) {
        lock_guard<mutex> guard(m_game_lock);
        m_player_connection.erase(id);
        m_player_status.erase(id);
    }

    void disconnect(player_id id) {
        lock_guard<mutex> guard(m_game_lock);
        m_player_status[id] = false;
    }

    void broadcast(const std::string& text) {
        lock_guard<mutex> guard(m_game_lock);

        for (auto it = m_player_connection.begin(); it != m_player_connection.end(); ++it) {
            player_id id = it->first;
            connection_hdl hdl = it->second;
            if(m_player_status[id]) {
                m_server_ptr->send(hdl, text, websocketpp::frame::opcode::text);
            }
        }
    }

    void update(long delta_time) {
        std::cout << "Update with timestep: " << delta_time << std::endl;
        m_time += delta_time;
    }
private:
    std::map<player_id, connection_hdl> m_player_connection;
    std::map<player_id, bool> m_player_status;
    server* m_server_ptr;
    mutex m_game_lock;
    long m_time = 0;
};

class main_server {
public:
    main_server() {
        // Initialize Asio Transport
        m_server.init_asio();

        // Register handler callbacks
        m_server.set_open_handler(bind(&main_server::on_open,this,::_1));
        m_server.set_close_handler(bind(&main_server::on_close,this,::_1));
        m_server.set_message_handler(bind(&main_server::on_message,this,::_1,::_2));
    }

    void run(uint16_t port) {
        // listen on specified port
        m_server.listen(port);

        // Start the server accept loop
        m_server.start_accept();

        // Start the ASIO io_service run loop
        try {
            m_server.run();
        } catch (const std::exception & e) {
            std::cout << e.what() << std::endl;
        }
    }

    void on_open(connection_hdl hdl) {
        {
            lock_guard<mutex> guard(m_action_lock);
            std::cout << "on_open" << std::endl;
            m_actions.push(action(SUBSCRIBE,hdl));
        }
        m_action_cond.notify_one();
    }

    void on_close(connection_hdl hdl) {
        {
            lock_guard<mutex> guard(m_action_lock);
            std::cout << "on_close" << std::endl;
            m_actions.push(action(UNSUBSCRIBE,hdl));
        }
        m_action_cond.notify_one();
    }

    void on_message(connection_hdl hdl, server::message_ptr msg) {
        // queue message up for sending by processing thread
        {
            lock_guard<mutex> guard(m_action_lock);
            std::cout << "on_message: " << msg->get_payload() << std::endl;
            m_actions.push(action(MESSAGE,hdl,msg));
        }
        m_action_cond.notify_one();
    }

    void setup_id(connection_hdl hdl, const std::string& text) {
        json login_json = json::parse(text, nullptr, false);

        if(login_json.is_discarded()) {
            std::cout << "login message not valid json" << std::endl;
            return;
        }
        if(login_json.contains("data")) {
            json data_json = login_json.at("data");
            if(data_json.contains("id")) {
                lock_guard<mutex> player_guard(m_player_lock);

                player_id id = data_json.at("id");
                m_connections.erase(hdl);
                m_player_map[hdl] = id;

                lock_guard<mutex> game_guard(m_game_list_lock);

                if(m_game_map.count(id) < 1) {
                    std::cout << "assigning connection to id: " << id << std::endl;
                    // this player id is new, put it in the new player list
                    m_players[id] = hdl;

                    // notify the matchmaker that a new player is available
                    m_match_cond.notify_one();
                }
                else {
                    std::cout << "reconnecting id: " << id << std::endl;
                    // this player is reconnecting
                    m_game_map[id]->reconnect(id, hdl);
                }
            }
        }
    }

    void process_messages() {
        while(1) {
            unique_lock<mutex> action_lock(m_action_lock);

            while(m_actions.empty()) {
                m_action_cond.wait(action_lock);
            }

            action a = m_actions.front();
            m_actions.pop();

            action_lock.unlock();

            if (a.type == SUBSCRIBE) {
                lock_guard<mutex> player_guard(m_player_lock);
                m_connections.insert(a.hdl);
            } else if (a.type == UNSUBSCRIBE) {
                lock_guard<mutex> player_guard(m_player_lock);
                if(m_player_map.count(a.hdl) < 1) {
                    std::cout << "player disconnected without providing id"
                        << std::endl;
                    // connection did not provide a player id
                    m_connections.erase(a.hdl);
                } else {
                    // connection provided a player id
                    player_id id = m_player_map[a.hdl];

                    std::cout << "player " << m_player_map[a.hdl]
                        << " disconnected" << std::endl;
 
                    lock_guard<mutex> game_guard(m_game_list_lock);
                    if(m_game_map.count(id) < 1) {
                        // player was not matched to a game 
                        m_players.erase(id);
                        m_player_map.erase(a.hdl);
                    }
                    else {
                        // player was matched to game, so notify it
                        m_game_map[id]->disconnect(id);
                        m_player_map.erase(a.hdl);
                    }
                }
            } else if (a.type == MESSAGE) {
                unique_lock<mutex> player_lock(m_player_lock);

                if(m_player_map.count(a.hdl) < 1) {
                    std::cout << "recieved message from connection with no id" << std::endl;
                    player_lock.unlock();
                    // player id not setup, must be a login message
                    setup_id(a.hdl, a.msg->get_payload());
                }
                else {
                    // player id already setup, route to their game
                    player_id id = m_player_map[a.hdl];
                    player_lock.unlock();

                    std::cout << "received message from id: " << id << std::endl;

                    lock_guard<mutex> game_guard(m_game_list_lock);
                    if(m_game_map.count(id)) {
                        m_game_map[id]->broadcast(a.msg->get_payload());
                    }
                }
            } else {
                // undefined.
            }
        }
    }

    void match_players() {
        while(1) {
            unique_lock<mutex> lock(m_player_lock);
            
            while(m_players.size() < 2) {
                m_match_cond.wait(lock);
            }
            
            auto gs = make_shared<game_server>(&m_server);

            auto it1 = m_players.begin();
            auto it2 = ++m_players.begin();
            player_id p1_id = it1->first;
            connection_hdl p1_hdl = it1->second;
            player_id p2_id = it2->first;
            connection_hdl p2_hdl = it2->second;
            m_players.erase(p1_id);
            m_players.erase(p2_id);

            std::cout << "creating game between " << p1_id
                << " and " << p2_id << std::endl;

            gs->add_player(p1_id, p1_hdl);
            gs->add_player(p2_id, p2_hdl);

            lock_guard<mutex> guard(m_game_list_lock);
            m_game_map[p1_id] = gs;
            m_game_map[p2_id] = gs;
            m_games.push_back(gs);
        }
    }

    void update_games() {
        auto time_start = std::chrono::high_resolution_clock::now();
        while(1) { 
            auto delta_time =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::high_resolution_clock::now() - time_start);
            if(delta_time >= timestep) {
                lock_guard<mutex> guard(m_game_list_lock);
                time_start = std::chrono::high_resolution_clock::now();
                
                // consider using std::execution::par_unseq here !!
                for(auto & gs : m_games) {
                    gs->update(delta_time.count());
                }
            }
            std::this_thread::sleep_for(std::min(1ms, delta_time));
        }
    }

private:
    typedef std::set<connection_hdl,std::owner_less<connection_hdl> > con_list;
    typedef std::map<
            connection_hdl,
            player_id,
            std::owner_less<connection_hdl>
        > con_map;

    server m_server;

    con_map m_player_map;
    std::map<player_id, std::shared_ptr<game_server> > m_game_map;
    std::queue<action> m_actions;

    mutex m_action_lock;
    mutex m_game_list_lock;
    mutex m_player_lock;
    condition_variable m_action_cond;
    condition_variable m_match_cond;
 
    con_list m_connections;
    std::map<player_id, connection_hdl> m_players;
    std::vector<std::shared_ptr<game_server> > m_games;
};

#endif // CATACRAWL_GAME_SERVER_HPP
