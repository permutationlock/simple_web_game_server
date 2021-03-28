#include <thread>
#include <functional>
#include <spdlog/spdlog.h>

#define DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

#include <simple_web_game_server/game_server.hpp>
#include <json_traits/nlohmann_traits.hpp>
#include <websocketpp_configs/asio_tls_no_logs.hpp>

#include "../tic_tac_toe_game.hpp"

using namespace std::chrono_literals;
using ttt_server = simple_web_game_server::game_server<
    tic_tac_toe_game,
    jwt::default_clock, nlohmann_traits,
    asio_tls_no_logs
  >;

using claim = jwt::basic_claim<nlohmann_traits>;
using combined_id = tic_tac_toe_player_traits::id;

using context_ptr =
  websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context>;

int main() {
  // log level
  spdlog::set_level(spdlog::level::trace);

  // create a jwt verifier
  jwt::verifier<jwt::default_clock, nlohmann_traits> 
    verifier(jwt::default_clock{});
  verifier.allow_algorithm(jwt::algorithm::hs256("secret"))
    .with_issuer("tic_tac_toe_matchmaker");

  // create a function to sign game result tokens
  auto sign_game = [](const combined_id& id, const json& data){
      json token_data;
      token_data["players"] = data["players"];
      token_data["scores"] = data["scores"];
      std::string token = jwt::create<nlohmann_traits>()
        .set_issuer("tic_tac_toe_game_server")
        .set_payload_claim("pid", claim(id.player))
        .set_payload_claim("sid", claim(id.session))
        .set_expires_at(std::chrono::system_clock::now()
          + std::chrono::seconds{1800})
        .set_payload_claim("data", claim(token_data))
        .sign(jwt::algorithm::hs256{"secret"});
      json temp = data;
      temp["token"] = token;
      return temp.dump();
    };

  // create a function to handle tls handshakes
  auto tls_init = [](websocketpp::connection_hdl hdl) {
      namespace asio = websocketpp::lib::asio;

      spdlog::debug("on_tls_init called with hdl {}", hdl.lock().get());

      context_ptr ctx = websocketpp::lib::make_shared<asio::ssl::context>(
          asio::ssl::context::sslv23
        );

      try {
          ctx->set_options(
              asio::ssl::context::default_workarounds |
              asio::ssl::context::no_sslv2 |
              asio::ssl::context::no_sslv3 |
              asio::ssl::context::no_tlsv1 |
              asio::ssl::context::single_dh_use
            );

          //ctx->set_password_callback(bind(&get_password));
          ctx->use_certificate_chain_file("../cert.pem");
          ctx->use_private_key_file(
              "../key.pem",
              asio::ssl::context::pem
            );
          
          ctx->use_tmp_dh_file("../dh.pem");
          
          std::string ciphers = "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:kEDH+AESGCM:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES256-SHA:DHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA:DHE-RSA-AES256-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!3DES:!MD5:!PSK";

          if (SSL_CTX_set_cipher_list(ctx->native_handle() , ciphers.c_str()) != 1) {
            spdlog::error("error setting cipher list");
          }
      } catch (std::exception& e) {
        spdlog::error("exception: {}", e.what());
      }
      return ctx;
    };

  // create our main server to manage player connection and matchmaking
  ttt_server gs(verifier, sign_game, 60s);

  gs.set_tls_init_handler(tls_init);

  // any of the processes below can be managed by multiple threads for higher
  // performance on multi-threaded machines

  std::thread gs_server_thr{bind(&ttt_server::run, &gs, 9090, true)};

  while(!gs.is_running()) {
    std::this_thread::sleep_for(10ms);
  }

  // bind a thread to manage websocket messages
  std::thread msg_process_thr{bind(&ttt_server::process_messages,&gs)};

  // bind a thread to update all running games at regular time steps
  std::thread game_thr{bind(&ttt_server::update_games, &gs, 100ms)};

  gs_server_thr.join();
  msg_process_thr.join();
  game_thr.join();
}
