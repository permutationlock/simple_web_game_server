#include <thread>
#include <chrono>
#include <functional>
#include <spdlog/spdlog.h>

#define DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

#include <simple_web_game_server/matchmaking_server.hpp>
#include <json_traits/nlohmann_traits.hpp>
#include <websocketpp_configs/asio_tls_no_logs.hpp>

#include "../minimal_game.hpp"

using namespace std::chrono_literals;
using claim = jwt::basic_claim<nlohmann_traits>;
using combined_id = minimal_matchmaker::player_traits::id;

using matchmaking_server = simple_web_game_server::matchmaking_server<
    minimal_matchmaker,
    jwt::default_clock, nlohmann_traits,
    asio_tls_no_logs
  >;

using context_ptr =
  websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context>;

int main() {
  // log level
  spdlog::set_level(spdlog::level::trace);

  const std::string secret = "secret";

  // create a jwt verifier
  jwt::verifier<jwt::default_clock, nlohmann_traits> 
    verifier(jwt::default_clock{});
  verifier.allow_algorithm(jwt::algorithm::hs256(secret))
    .with_issuer("auth_server");

  // create a function to sign game tokens
  auto sign_game = [=](combined_id id, const json& data){ 
      std::string token = jwt::create<nlohmann_traits>()
        .set_issuer("matchmaking_server")
        .set_payload_claim("pid", claim(id.player))
        .set_payload_claim("sid", claim(id.session))
        .set_payload_claim("data", claim(data))
        .sign(jwt::algorithm::hs256{secret});
      json temp = { { "type", "token" }, { "token", token } };
      return temp.dump();
    };

  // create a function to handle tls handshakes
  auto tls_init = [](websocketpp::connection_hdl hdl) {
      namespace asio = websocketpp::lib::asio;

      spdlog::trace("on_tls_init called with hdl {}", hdl.lock().get());

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
  matchmaking_server mms{verifier, sign_game, 60s};

  mms.set_tls_init_handler(tls_init);

  std::thread mms_server_thr{bind(&matchmaking_server::run, &mms, 9091, true)};

  while(!mms.is_running()) {
    std::this_thread::sleep_for(10ms);
  }

  std::thread msg_process_thr{
      bind(&matchmaking_server::process_messages, &mms)
    };

  std::thread match_thr{bind(&matchmaking_server::match_players, &mms, 10ms)};

  mms_server_thr.join();
  msg_process_thr.join();
  match_thr.join();
}
