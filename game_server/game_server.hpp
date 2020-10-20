#ifndef CATACRAWL_GAME_SERVER_HPP
#define CATACRAWL_GAME_SERVER_HPP

#include <websocketpp/config/asio_no_tls.hpp>

#include <websocketpp/server.hpp>

#include <iostream>
#include <set>
#include <mutex>
#include <condition_variable>
#include <chrono>

using namespace std::chrono_literals;

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

    void add_player(connection_hdl hdl) {
        lock_guard<mutex> guard(m_connection_lock);
        m_connections.insert(hdl);
    }

    void remove_player(connection_hdl hdl) {
        lock_guard<mutex> guard(m_connection_lock);

        m_connections.erase(hdl);
    }

    void broadcast(std::string msg) {
        lock_guard<mutex> guard(m_connection_lock);

        for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
            m_server_ptr->send(*it, msg, websocketpp::frame::opcode::text);
        }
    }

    void update(long delta_time) {
        std::cout << "Update with timestep: " << delta_time << std::endl;
        m_time += delta_time;
    }
private:
    typedef std::set<connection_hdl,std::owner_less<connection_hdl> > con_list;

    con_list m_connections;
    server* m_server_ptr;
    mutex m_connection_lock;
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
        m_match_cond.notify_one();
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
            std::cout << "on_message" << std::endl;
            m_actions.push(action(MESSAGE,hdl,msg));
        }
        m_action_cond.notify_one();
    }

    void process_messages() {
        while(1) {
            unique_lock<mutex> lock(m_action_lock);

            while(m_actions.empty()) {
                m_action_cond.wait(lock);
            }

            action a = m_actions.front();
            m_actions.pop();

            lock.unlock();

            if (a.type == SUBSCRIBE) {
                lock_guard<mutex> guard(m_connection_lock);
                m_connections.insert(a.hdl);
            } else if (a.type == UNSUBSCRIBE) {
                lock_guard<mutex> guard(m_connection_lock);
                if(m_connections.count(a.hdl)) {
                    m_connections.erase(a.hdl);
                } else {
                    std::cout << "removing player from game" << std::endl;
                    m_game_map[a.hdl]->remove_player(a.hdl);
                    m_game_map.erase(a.hdl);
                }
            } else if (a.type == MESSAGE) {
                if(m_game_map.count(a.hdl)) {
                    m_game_map[a.hdl]->broadcast(a.msg->get_payload());
                }
            } else {
                // undefined.
            }
        }
    }

    void match_players() {
        while(1) {
            unique_lock<mutex> lock(m_connection_lock);
            
            while(m_connections.size() < 2) {
                m_match_cond.wait(lock);
            }
            
            std::cout << "creating game" << std::endl;
            auto gs = make_shared<game_server>(&m_server);

            connection_hdl player_1 = *(m_connections.begin());
            connection_hdl player_2 = *(++m_connections.begin());

            gs->add_player(player_1);
            gs->add_player(player_2);

            m_game_map[player_1] = gs;
            m_game_map[player_2] = gs;

            m_connections.erase(player_1);
            m_connections.erase(player_2);

            unique_lock<mutex> glock(m_game_list_lock);
            games.push_back(gs);
        }
    }

    void update_games() {
        auto time_start = std::chrono::high_resolution_clock::now();
        while(1) { 
            auto delta_time =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::high_resolution_clock::now() - time_start);
            if(delta_time >= timestep) {
                unique_lock<mutex> lock(m_game_list_lock);
                time_start = std::chrono::high_resolution_clock::now();
                
                // consider using std::execution::par_unseq here !!
                for(auto & gs : games) {
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
            std::shared_ptr<game_server>,
            std::owner_less<connection_hdl>
        > con_map;

    server m_server;
    con_list m_connections;
    con_map m_game_map;
    std::queue<action> m_actions;

    mutex m_action_lock;
    mutex m_connection_lock;
    mutex m_game_list_lock;
    condition_variable m_action_cond;
    condition_variable m_match_cond;

    std::vector<std::shared_ptr<game_server> > games;
};

#endif // CATACRAWL_GAME_SERVER_HPP
