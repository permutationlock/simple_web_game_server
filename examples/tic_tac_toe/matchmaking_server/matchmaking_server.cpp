#include <filesystem>
#include <thread>
#include <chrono>
#include <functional>
#include <fstream>
#include <cmath>
#include <string>
#include <spdlog/spdlog.h>

#define DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

#include <simple_web_game_server/matchmaking_server.hpp>
#include <json_traits/nlohmann_traits.hpp>
#include <websocketpp_configs/asio_tls_no_logs.hpp>

#include "../tic_tac_toe_game.hpp"

const size_t MAX_PIDS = 10000000;

const std::string secret = "secret";

using namespace std::chrono_literals;
using claim = jwt::basic_claim<nlohmann_traits>;
using combined_id = tic_tac_toe_player_traits::id;

using ttt_server = simple_web_game_server::matchmaking_server<
    tic_tac_toe_matchmaker,
    jwt::default_clock, nlohmann_traits,
    asio_tls_no_logs
  >;

using context_ptr =
  websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context>;

struct player_data {
  int elo;
  bool matchmaking;
  combined_id::session_id sid;
  std::chrono::time_point<std::chrono::system_clock> exp_time;
};

combined_id::session_id session_count = 0;
std::vector<player_data> player_database;
jwt::verifier<jwt::default_clock, nlohmann_traits> auth_verifier(
  jwt::default_clock{}
);
jwt::verifier<jwt::default_clock, nlohmann_traits> mm_verifier(
  jwt::default_clock{}
);
jwt::verifier<jwt::default_clock, nlohmann_traits> game_verifier(
  jwt::default_clock{}
);

void http_handler(ttt_server::connection_ptr conn) {
  auto const &req = conn->get_request();
  const std::string &method = req.get_method();
  const std::string &uri = req.get_uri();
  spdlog::debug("request:\n    method: {}\n    uri: {}\n", method, uri);
  try {
    if (method.compare("GET") != 0) {
      conn->set_status(websocketpp::http::status_code::method_not_allowed);
      return;
    }
    if (uri.compare("/signup") == 0 || uri.compare("/signup/") == 0) {
      if (player_database.size() >= MAX_PIDS) {
        spdlog::error("out of PIDS");
      }
      conn->set_status(websocketpp::http::status_code::ok);
      std::string token = jwt::create<nlohmann_traits>()
        .set_issuer("tic_tac_toe_auth")
        .set_payload_claim("pid",
          claim((combined_id::player_id)player_database.size()))
        .sign(jwt::algorithm::hs256{secret});
      player_database.push_back({ .elo = 1500 });
      conn->set_body(token);
      return;
    }
    const std::string info("/info/");
    if (uri.substr(0, info.size()).compare(info) == 0) {
      const std::string token = uri.substr(info.size(), uri.size());
      try {
        jwt::decoded_jwt<nlohmann_traits> decoded = jwt::decode<nlohmann_traits>(
          token
        );
        auth_verifier.verify(decoded);
        auto claim_map = decoded.get_payload_claims();
        combined_id::player_id pid =
          tic_tac_toe_player_traits::parse_player_id(
            claim_map.at("pid").to_json()
          );
        
        json resp_json;
        resp_json["success"] = true;
        resp_json["pid"] = pid;
        resp_json["rating"] = player_database.at(pid).elo;
        conn->set_status(websocketpp::http::status_code::ok);
        conn->set_body(resp_json.dump());
        return;
      } catch (std::exception &e) {
        spdlog::debug("invalid jwt /info/{}: {}", token, e.what());
        return;
      }
    }

    const std::string login("/login/");
    if (uri.substr(0, login.size()).compare(login) == 0) {
      const std::string token = uri.substr(login.size(), uri.size());
      try {
        jwt::decoded_jwt<nlohmann_traits> decoded = jwt::decode<nlohmann_traits>(
          token
        );
        auth_verifier.verify(decoded);
        auto claim_map = decoded.get_payload_claims();
        combined_id::player_id pid =
          tic_tac_toe_player_traits::parse_player_id(
            claim_map.at("pid").to_json()
          );

        if (!player_database.at(pid).matchmaking) {
          player_database.at(pid).exp_time = std::chrono::system_clock::now()
            + std::chrono::seconds{1800};
          player_database.at(pid).sid = session_count++;
          player_database.at(pid).matchmaking = true;
        }

        json data_json;
        data_json["rating"] = player_database.at(pid).elo;

        std::string match_token = jwt::create<nlohmann_traits>()
          .set_issuer("tic_tac_toe_auth")
          .set_payload_claim("pid", claim(pid))
          .set_payload_claim("sid", claim(player_database.at(pid).sid))
          .set_expires_at(player_database.at(pid).exp_time)
          .set_payload_claim("data", claim(data_json))
          .sign(jwt::algorithm::hs256{secret});
        conn->set_status(websocketpp::http::status_code::ok);
        conn->set_body(match_token);
        return;
      } catch (std::exception &e) {
        spdlog::debug("invalid jwt /login/{}: {}", token, e.what());
        return;
      }
    }

    const std::string submit("/submit/");
    if (uri.substr(0, submit.size()).compare(submit) == 0) {
      const std::string token = uri.substr(submit.size(), uri.size());
      try {
        jwt::decoded_jwt<nlohmann_traits> decoded = jwt::decode<nlohmann_traits>(
          token
        );
        game_verifier.verify(decoded);
        auto claim_map = decoded.get_payload_claims();
        combined_id::player_id pid =
          tic_tac_toe_player_traits::parse_player_id(
            claim_map.at("pid").to_json()
          );
        json data_json = claim_map.at("data").to_json();

        if (player_database.at(pid).matchmaking) {
          auto players = data_json["players"]
            .get<vector<combined_id::player_id> >();
          auto scores = data_json["scores"].get<vector<double> >();

          player_data &p1 = player_database.at(players.at(0));
          player_data &p2 = player_database.at(players.at(1));

          double q1 = std::pow(10.0, p1.elo / 400.0);
          double q2 = std::pow(10.0, p2.elo / 400.0);

          double denom = q1 + q2;

          double expected_s1 = q1 / denom;
          double expected_s2 = q2 / denom;

          p1.elo += 32.0 * (scores.at(0) - expected_s1);
          p2.elo += 32.0 * (scores.at(1) - expected_s2);
          
          spdlog::debug("match reported, rating updated");
          player_database.at(pid).matchmaking = false;
        }

        json resp_json;
        resp_json["success"] = true;

        conn->set_status(websocketpp::http::status_code::ok);
        conn->set_body(resp_json.dump());
        return;
      } catch (std::exception &e) {
        spdlog::debug("invalid jwt /submit/{}: {}", token, e.what());
        json resp_json;
        resp_json["success"] = false;

        conn->set_status(websocketpp::http::status_code::ok);
        conn->set_body(resp_json.dump());
        return;
      }
    }

    const std::string cancel("/cancel/");
    if (uri.substr(0, cancel.size()).compare(cancel) == 0) {
      const std::string token = uri.substr(cancel.size(), uri.size());
      try {
        jwt::decoded_jwt<nlohmann_traits> decoded = jwt::decode<nlohmann_traits>(
          token
        );
        mm_verifier.verify(decoded);
        auto claim_map = decoded.get_payload_claims();
        combined_id::player_id pid =
          tic_tac_toe_player_traits::parse_player_id(
            claim_map.at("pid").to_json()
          );
        json data_json = claim_map.at("data").to_json();

        json resp_json;
        if (player_database.at(pid).matchmaking) {
          if (!data_json["matched"].get<bool>()) {
            spdlog::debug("matchmaking aborted");
            player_database.at(pid).matchmaking = false;
            resp_json["success"] = true;
          } else {
            resp_json["success"] = false;
          }
        } else {
          resp_json["success"] = false;
        }

        conn->set_status(websocketpp::http::status_code::ok);
        conn->set_body(resp_json.dump());
        return;
      } catch (std::exception &e) {
        spdlog::debug("invalid jwt /cancel/{}: {}", token, e.what());
        json resp_json;
        resp_json["success"] = false;

        conn->set_status(websocketpp::http::status_code::ok);
        conn->set_body(resp_json.dump());
        return;
      }
    }

    std::filesystem::path cwd = std::filesystem::current_path();
    std::filesystem::path static_path = cwd / "public_html";
    std::filesystem::path file_path(static_path.string() + uri);

    if (uri.compare("/") == 0) {
      file_path = static_path / "index.html";
    } else {
      try {
        // file_path = static_path / std::filesystem::path(uri);
        file_path = std::filesystem::canonical(file_path);
      } catch (std::exception &e) {
        spdlog::debug("uri non-canonical: {}", e.what());
        return;
      }
    }

    if (static_path > file_path) {
      conn->set_status(websocketpp::http::status_code::not_found);
      spdlog::debug("invalid uri path: {}", file_path.string());
      return;
    }

    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs.is_open()) {
      conn->set_status(websocketpp::http::status_code::not_found);
      spdlog::debug("file not found: {}", file_path.string());
      return;
    }

    std::string content(
      (std::istreambuf_iterator<char>(ifs)),
      (std::istreambuf_iterator<char>())
    );
    // spdlog::debug("responding with body: {}", content);
    conn->set_status(websocketpp::http::status_code::ok);
    conn->set_body(content);
  } catch (std::exception &e) {
    spdlog::debug("response failed to update: {}", e.what());
  }
}

int main() {
  // log level
  spdlog::set_level(spdlog::level::trace);

  // reserver player database memory
  player_database.reserve(MAX_PIDS);

  // create jwt verifiers
  auth_verifier = jwt::verifier<jwt::default_clock, nlohmann_traits>(jwt::default_clock{});
  auth_verifier.allow_algorithm(jwt::algorithm::hs256(secret))
    .with_issuer("tic_tac_toe_auth");

  mm_verifier = jwt::verifier<jwt::default_clock, nlohmann_traits>(jwt::default_clock{});
  mm_verifier.allow_algorithm(jwt::algorithm::hs256(secret))
    .with_issuer("tic_tac_toe_matchmaker");

  game_verifier = jwt::verifier<jwt::default_clock, nlohmann_traits>(jwt::default_clock{});
  game_verifier.allow_algorithm(jwt::algorithm::hs256(secret))
    .with_issuer("tic_tac_toe_game_server");

  // create a function to sign game tokens
  auto sign_game = [=](combined_id id, const json& data){ 
      return jwt::create<nlohmann_traits>()
        .set_issuer("tic_tac_toe_matchmaker")
        .set_payload_claim("pid", claim(id.player))
        .set_payload_claim("sid", claim(id.session))
        .set_expires_at(std::chrono::system_clock::now()
          + std::chrono::seconds{1800})
        .set_payload_claim("data", claim(data))
        .sign(jwt::algorithm::hs256{secret});
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
  ttt_server mms{auth_verifier, sign_game, 60s};

  mms.set_tls_init_handler(tls_init);
  mms.set_http_handler(http_handler);

  std::thread mms_server_thr{bind(&ttt_server::run, &mms, 9091, true)};

  while(!mms.is_running()) {
    std::this_thread::sleep_for(10ms);
  }

  std::thread msg_process_thr{bind(&ttt_server::process_messages, &mms)};

  std::thread match_thr{bind(&ttt_server::match_players, &mms, 10ms)};

  mms_server_thr.join();
  msg_process_thr.join();
  match_thr.join();
}
