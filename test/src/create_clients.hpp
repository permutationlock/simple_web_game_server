#ifndef CREATE_CLIENTS_HPP
#define CREATE_CLIENTS_HPP

#include <vector>
#include <chrono>
#include <functional>
#include <thread>

// Create player_count clients running in a thread and using the handlers
// provided in client_data. It is expected that clients, client_data_list, and
// client_threads are all empty vectors which will be filled by this call.
// The vector of tokens is expected to contain player_count JWT token strings.
// Clients will connect using their corresponding token to the server at the
// provided uri.
template<typename player_id, typename game_client, typename client_data>
void create_clients(
      std::vector<game_client>& clients,
      std::vector<client_data>& client_data_list,
      std::vector<std::thread>& client_threads,
      const std::vector<std::string>& tokens,
      const std::string& uri,
      std::size_t player_count
    ) {
  using namespace std::chrono_literals;
  using std::placeholders::_1;
  using std::placeholders::_2;

  for(std::size_t i = 0; i < player_count; i++) {
    clients.push_back(game_client{});
    client_data_list.push_back(client_data{});
  }

  for(std::size_t i = 0; i < player_count; i++) {
    auto open_handler = std::bind(&client_data::on_open,
        &(client_data_list[i]));
    clients[i].set_open_handler(open_handler);
    auto close_handler = std::bind(&client_data::on_close,
        &(client_data_list[i]));
    clients[i].set_close_handler(close_handler);
    auto message_handler = std::bind(&client_data::on_message,
        &(client_data_list[i]), _1);
    clients[i].set_message_handler(message_handler);
  }

  for(std::size_t i = 0; i < player_count; i++) {
    std::thread client_thr{
        bind(&game_client::connect, &(clients[i]), uri, tokens[i])
      };

    while(!clients[i].is_running()) {
      std::this_thread::sleep_for(1ms);
    }

    client_threads.push_back(std::move(client_thr));
  }
}

#endif // CREATE_CLIENTS_HPP
