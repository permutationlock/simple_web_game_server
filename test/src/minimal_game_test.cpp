#include <doctest/doctest.h>
#include <nlohmann/json.hpp>
#include <model_games/minimal_game.hpp>

#include <vector>
#include <map>
#include <set>

TEST_CASE("minimal games are constructed from json { \"players\": [..] }") {
  using json = nlohmann::json;
  using std::map;
  using std::vector;

  minimal_game empty_players{json::parse("{ \"players\": [] }")};
  CHECK(empty_players.is_valid() == true);
  CHECK(empty_players.get_player_list().size() == 0);

  minimal_game one_player{json::parse("{ \"players\": [14] }")};
  CHECK(one_player.is_valid() == true);
  CHECK(one_player.get_player_list().size() == 1);
  CHECK(one_player.get_player_list()[0] == 14);

  minimal_game fourteen_players{
    json::parse("{ \"players\": [2, 19, 218, 14131, 123, 0, 9] }")};
  CHECK(fourteen_players.is_valid() == true);
  CHECK(fourteen_players.get_player_list().size() == 7);
  CHECK(fourteen_players.get_player_list()[0] == 2);
  CHECK(fourteen_players.get_player_list()[1] == 19);
  CHECK(fourteen_players.get_player_list()[2] == 218);
  CHECK(fourteen_players.get_player_list()[3] == 14131);
  CHECK(fourteen_players.get_player_list()[4] == 123);
  CHECK(fourteen_players.get_player_list()[5] == 0);
  CHECK(fourteen_players.get_player_list()[6] == 9);

  minimal_game empty_json{json::parse("{}")};
  CHECK(empty_json.is_valid() == false);

  minimal_game non_list_players{json::parse("{ \"players\": 21 }")};
  CHECK(non_list_players.is_valid() == false);

  minimal_game string_players{json::parse("{ \"players\": [\"a\"] }")};
  CHECK(string_players.is_valid() == false);

  minimal_game object_players{json::parse("{ \"players\": [{ \"id\": 21 }] }")};
  CHECK(object_players.is_valid() == false);
}

TEST_CASE("minimal games should be done and not have messages") {
  using json = nlohmann::json;

  minimal_game one_player{json::parse("{ \"players\": [83] }")};
  CHECK(one_player.is_done() == true);
  CHECK(one_player.has_message() == false);
}

TEST_CASE("matchmaking data game should track list of players and dump to json") {
  using json = nlohmann::json;
  using std::vector;

  using player_id = minimal_matchmaking_data::player_id;
  using game = minimal_matchmaking_data::game;

  SUBCASE("empty game should have an empty list") {
    game g{vector<player_id>{}};
    CHECK(g.player_list.size() == 0);
    CHECK(g.data == json::parse("{\"players\":[]}"));
  }

  SUBCASE("game with two players should store correctly and parse to json") {
    game g{vector<player_id>{75, 34}};

    CHECK(g.player_list.size() == 2);
    CHECK(g.player_list[0] == 75);
    CHECK(g.player_list[1] == 34);
    CHECK(g.data == json::parse("{\"players\":[75,34]}"));
  }
}

TEST_CASE("matchmaking data match function should pair players in order") {
  using std::set;
  using std::map;
  using std::vector;

  using player_id = minimal_matchmaking_data::player_id;
  using player_data = minimal_matchmaking_data::player_data;
  using game = minimal_matchmaking_data::game;

  map<player_id, player_data> player_map;

  SUBCASE("empty map should return empty list of games") {
    vector<game> games = minimal_matchmaking_data::match(player_map);
    CHECK(games.size() == 0);
  }

  SUBCASE("map with two players should return one game") {
    player_map[43] = player_data{};
    player_map[102] = player_data{};
    vector<game> games = minimal_matchmaking_data::match(player_map);
    CHECK(games.size() == 1);

    set<player_id> players;
    for(const game& g : games) {
      for(player_id id : g.player_list) {
        players.insert(id);
      }
    }
    CHECK(players.size() == 2);
    for(player_id id : players) {
      CHECK(player_map.count(id) > 0);
    }
  }

  SUBCASE("map with seven players should return three games") {
    player_map[8] = player_data{};
    player_map[66] = player_data{};
    player_map[163] = player_data{};
    player_map[421] = player_data{};
    player_map[741] = player_data{};
    player_map[907] = player_data{};
    player_map[2001] = player_data{};
    vector<game> games = minimal_matchmaking_data::match(player_map);
    CHECK(games.size() == 3);

    set<player_id> players;
    for(const game& g : games) {
      for(player_id id : g.player_list) {
        players.insert(id);
      }
    }
    CHECK(players.size() == 6);
    for(player_id id : players) {
      CHECK(player_map.count(id) > 0);
    }
  }

  SUBCASE("we should not be able to match an empty player map") {
    CHECK(minimal_matchmaking_data::can_match(player_map) == false);
  }

  SUBCASE("we should not be able to match a player map with one player") {
    player_map[513] = player_data{};
    CHECK(minimal_matchmaking_data::can_match(player_map) == false);
  }

  SUBCASE("we should be able to match a player map with two player") {
    player_map[223] = player_data{};
    player_map[45112] = player_data{};
    CHECK(minimal_matchmaking_data::can_match(player_map) == true);
  }
}
