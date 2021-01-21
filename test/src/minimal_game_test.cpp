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

TEST_CASE("matchmaking data game should track player list and data json") {
  using json = nlohmann::json;
  using std::vector;

  using combined_id = minimal_matchmaker::combined_id;
  using game = minimal_matchmaker::game;

  SUBCASE("empty game should have an empty player_list") {
    game g{vector<combined_id>{}, 0};
    CHECK(g.player_list.size() == 0);
    CHECK(g.data.dump() == "{\"players\":[]}");
  }

  SUBCASE("game with two players should store correctly and parse to json") {
    game g{vector<combined_id>{ {75, 0}, {34, 0} }, 0};

    CHECK(g.player_list.size() == 2);
    CHECK(g.player_list[0].player == 75);
    CHECK(g.player_list[1].player == 34);
    CHECK(g.data == json::parse("{\"players\":[75,34]}"));
  }
}

TEST_CASE("matchmaking data match function should pair players") {
  using std::set;
  using std::map;
  using std::vector;

  using combined_id = minimal_matchmaker::combined_id;
  using player_data = minimal_matchmaker::player_data;
  using game = minimal_matchmaker::game;

  minimal_matchmaker matchmaker;
  map<combined_id, player_data> player_map;
  set<combined_id> altered_players;

  SUBCASE("empty map should return empty list of games") {
    vector<game> games = matchmaker.match(player_map, altered_players);
    CHECK(games.size() == 0);
  }

  SUBCASE("map with two players should return one game") {
    player_map[{ 43, 9 }] = player_data{};
    player_map[{ 102, 3241 }] = player_data{};
    vector<game> games = matchmaker.match(player_map, altered_players);
    CHECK(games.size() == 1);

    set<combined_id> players;
    for(const game& g : games) {
      for(combined_id id : g.player_list) {
        players.insert(id);
      }
    }
    CHECK(players.size() == 2);
    for(combined_id id : players) {
      CHECK(player_map.count(id) > 0);
    }
  }

  SUBCASE("map with seven players should return three games") {
    player_map[{ 8, 7 }] = player_data{};
    player_map[{ 66, 12 }] = player_data{};
    player_map[{ 163, 712 }] = player_data{};
    player_map[{ 421, 2 }] = player_data{};
    player_map[{ 741, 82 }] = player_data{};
    player_map[{ 907, 312 }] = player_data{};
    player_map[{ 2001, 10 }] = player_data{};
    vector<game> games = matchmaker.match(player_map, altered_players);
    CHECK(games.size() == 3);

    set<combined_id> players;
    for(const game& g : games) {
      for(combined_id id : g.player_list) {
        players.insert(id);
      }
    }
    CHECK(players.size() == 6);
    for(combined_id id : players) {
      CHECK(player_map.count(id) > 0);
    }
  }

  SUBCASE("we should not be able to match an empty player map") {
    CHECK(matchmaker.can_match(player_map, altered_players) == false);
  }

  SUBCASE("we should not be able to match a player map with one player") {
    player_map[{ 513, 9231 }] = player_data{};
    CHECK(matchmaker.can_match(player_map, altered_players) == false);
  }

  SUBCASE("we should be able to match a player map with two players") {
    player_map[{ 223, 17 }] = player_data{};
    player_map[{ 45112, 2}] = player_data{};
    CHECK(matchmaker.can_match(player_map, altered_players) == true);
  }
}
