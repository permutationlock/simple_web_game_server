#include <thread>
#include <functional>
#include <spdlog/spdlog.h>

#define DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

#include "game_server.hpp"
#include "tic_tac_toe_game.hpp"

using json = nlohmann::json;

// json traits to use nlohmann json library with jwt-cpp
struct nlohmann_traits {
  using json = nlohmann::json;
  using value_type = json;
  using object_type = json::object_t;
  using array_type = json::array_t;
  using string_type = std::string;
  using number_type = json::number_float_t;
  using integer_type = json::number_integer_t;
  using boolean_type = json::boolean_t;

  static jwt::json::type get_type(const json &val) {
    using jwt::json::type;

    if (val.type() == json::value_t::boolean)
      return type::boolean;
    else if (val.type() == json::value_t::number_integer)
      return type::integer;
    else if (val.type() == json::value_t::number_unsigned)
      return type::integer;
    else if (val.type() == json::value_t::number_float)
      return type::number;
    else if (val.type() == json::value_t::string)
      return type::string;
    else if (val.type() == json::value_t::array)
      return type::array;
    else if (val.type() == json::value_t::object)
      return type::object;
    else
      throw std::logic_error("invalid type");
  }

  static json::object_t as_object(const json &val) {
    if (val.type() != json::value_t::object)
      throw std::bad_cast();
    return val.get<json::object_t>();
  }

  static std::string as_string(const json &val) {
    if (val.type() != json::value_t::string)
      throw std::bad_cast();
    return val.get<std::string>();
  }

  static json::array_t as_array(const json &val) {
    if (val.type() != json::value_t::array)
      throw std::bad_cast();
    return val.get<json::array_t>();
  }

  static int64_t as_int(const json &val) {
    switch(val.type())
    {
      case json::value_t::number_integer:
      case json::value_t::number_unsigned:
        return val.get<int64_t>();
      default:  
        throw std::bad_cast();
    }
  }

  static bool as_bool(const json &val) {
    if (val.type() != json::value_t::boolean)
      throw std::bad_cast();
    return val.get<bool>();
  }

  static double as_number(const json &val) {
    if (val.type() != json::value_t::number_float)
      throw std::bad_cast();
    return val.get<double>();
  }

  static bool parse(json &val, std::string str) {
    val = json::parse(str.begin(), str.end());
    return true;
  }

  static std::string serialize(const json &val) { return val.dump(); }
};

typedef main_server<tic_tac_toe_game, jwt::default_clock, nlohmann_traits>
  ttt_server;

int main() {
  // log level
  spdlog::set_level(spdlog::level::trace);

  // create a jwt verifier
  jwt::verifier<jwt::default_clock, nlohmann_traits> 
    verifier(jwt::default_clock{});
  verifier.allow_algorithm(jwt::algorithm::hs256("passwd"))
    .with_issuer("krynth");

  // create our main server to manage player connection and matchmaking
  ttt_server gs(verifier);

  // any of the processes below can be managed by multiple threads for higher
  // performance on multi-threaded machines

  // bind a thread to manage websocket messages
  std::thread msg_process_thr(bind(&ttt_server::process_messages,&gs));

  // bind a thread to update all running games at regular time steps
  std::thread game_thr(bind(&ttt_server::update_games,&gs));
  
  gs.run(9090);
}
