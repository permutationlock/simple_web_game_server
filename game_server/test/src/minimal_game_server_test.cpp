#include <doctest/doctest.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/ostream_sink.h>

#define DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

#include <jwt_game_server/game_server.hpp>
#include <json_traits/nlohmann_traits.hpp>
#include <model_games/minimal_game.h>

#include <sstream>

TEST_CASE("test") {
  typedef jwt_game_server::game_server<
      minimal_game,
      jwt::default_clock,
      nlohmann_traits
    > minimal_game_server;

  std::ostringstream oss;
  auto ostream_sink = std::make_shared<spdlog::sinks::ostream_sink_mt> (oss);
  auto logger = std::make_shared<spdlog::logger>("my_logger", ostream_sink);

  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::err);
}
