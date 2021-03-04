#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "test_game.hpp"

TEST_CASE(
    "games should not be valid when constructed from json {\"matched\":false}"
  )
{
  using json = nlohmann::json;

  test_game one_player{json{{ "matched", false }}};

  CHECK(one_player.is_done() == false);
  CHECK(one_player.is_valid() == false);
}

TEST_CASE(
    "games should be valid when constructed from json {\"matched\":true}"
  )
{
  using json = nlohmann::json;

  test_game one_player{json{{ "matched", true }}};

  CHECK(one_player.is_done() == false);
  CHECK(one_player.is_valid() == true);
}

TEST_CASE(
    "games should not be valid when constructed from incorrect json"
  )
{
  using json = nlohmann::json;

  test_game one_player{json{{ "matched", "not a bool!" }}};

  CHECK(one_player.is_done() == false);
  CHECK(one_player.is_valid() == false);
}

TEST_CASE("matchmaker should provide json data for canceled sessions") {
  using json = nlohmann::json;

  test_matchmaker matchmaker;

  json inv_data = matchmaker.get_cancel_data();

  CHECK(inv_data["matched"] == false);
}

TEST_CASE("matchmaking data match function should pair players") {
  using std::unordered_set;
  using std::unordered_map;
  using std::vector;

  using session_id = test_player_traits::id::session_id;
  using id_hash = test_player_traits::id::hash;
  using session_data = test_matchmaker::session_data;
  using game = test_matchmaker::game;
  using message = test_matchmaker::message;

  test_matchmaker matchmaker;
  unordered_map<session_id, session_data, id_hash> session_map;
  vector<message> messages;
  vector<game> games;

  SUBCASE("empty map should return empty list of games") {
    matchmaker.match(games, messages, session_map, 0);

    CHECK(games.size() == 0);
  }

  SUBCASE("map with two players should return one game") {
    session_map.emplace(std::make_pair(9, session_data{json{}}));
    session_map.emplace(std::make_pair(3241, session_data{json{}}));
    matchmaker.match(games, messages, session_map, 0);

    CHECK(games.size() == 1);

    unordered_set<session_id, id_hash> sessions;
    for(const game& g : games) {
      for(session_id sid : std::get<0>(g)) {
        sessions.insert(sid);
      }
    }

    CHECK(sessions.size() == session_map.size());

    std::size_t matched_session_count = 0;
    for(auto& spair : session_map) {
      if(sessions.count(spair.first) > 0) {
        matched_session_count++;
      }
    }

    CHECK(matched_session_count == 2);
  }

  SUBCASE("map with seven players should return three games") {
    session_map.emplace(std::make_pair(7, session_data{json{}}));
    session_map.emplace(std::make_pair(12, session_data{json{}}));
    session_map.emplace(std::make_pair(712, session_data{json{}}));
    session_map.emplace(std::make_pair(2, session_data{json{}}));
    session_map.emplace(std::make_pair(82, session_data{json{}}));
    session_map.emplace(std::make_pair(312, session_data{json{}}));
    session_map.emplace(std::make_pair(10, session_data{json{}}));
    matchmaker.match(games, messages, session_map, 0);

    CHECK(games.size() == 3);

    unordered_set<session_id, id_hash> sessions;
    for(const game& g : games) {
      for(session_id sid : std::get<0>(g)) {
        sessions.insert(sid);
      }
    }

    CHECK(sessions.size() == 6);

    std::size_t matched_session_count = 0;
    for(auto& spair : session_map) {
      if(sessions.count(spair.first) > 0) {
        matched_session_count++;
      }
    }

    CHECK(matched_session_count == 6);
  }

  SUBCASE("we should not be able to match an empty session map") {
    CHECK(matchmaker.can_match(session_map) == false);
  }

  SUBCASE("we should not be able to match a session map with one session") {
    session_map.emplace(std::make_pair(9231, session_data{json{}}));

    CHECK(matchmaker.can_match(session_map) == false);
  }

  SUBCASE("we should be able to match a session map with two sessions") {
    session_map.emplace(std::make_pair(17, session_data{json{}}));
    session_map.emplace(std::make_pair(2, session_data{json{}}));

    CHECK(matchmaker.can_match(session_map) == true);
  }
}
