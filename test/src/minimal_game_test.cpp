#include <doctest/doctest.h>
#include <nlohmann/json.hpp>
#include <model_games/minimal_game.hpp>

#include <vector>
#include <unordered_map>
#include <unordered_set>

TEST_CASE("minimal games should be done and not have messages") {
  using json = nlohmann::json;

  minimal_game one_player{json{}};

  CHECK(one_player.is_done() == true);
  CHECK(one_player.has_message() == false);
}

TEST_CASE("matchmaker games should track player list and data json") {
  using std::vector;

  using session_id = minimal_player_traits::id::session_id;
  using game = minimal_matchmaker::game;

  SUBCASE("empty game should have an empty session_list") {
    game g{vector<session_id>{}, 0};

    CHECK(g.session_list.size() == 0);
    CHECK(g.data.dump() == "{\"matched\":true}");
  }

  SUBCASE("game with two players should store correctly and parse to json") {
    game g{vector<session_id>{8, 915}, 87};

    CHECK(g.session_list.size() == 2);
    CHECK(g.session_list == std::vector<session_id>{8, 915});
    CHECK(g.session == 87);
    CHECK(g.data["matched"] == true);
  }
}

TEST_CASE("matchmaker should generate simple cancel json") {
  using json = nlohmann::json;

  using session_data = minimal_matchmaker::session_data;

  minimal_matchmaker matchmaker;

  session_data data{json{}};
  json cancel_data = matchmaker.get_cancel_data(471, data);

  CHECK(cancel_data["matched"] == false);
}


TEST_CASE("matchmaker game should track session list and data json") {
  using std::vector;

  using session_id = minimal_player_traits::id::session_id;
  using game = minimal_matchmaker::game;

  SUBCASE("empty game should have an empty session_list") {
    game g{vector<session_id>{}, 0};

    CHECK(g.session_list.size() == 0);
    CHECK(g.data.dump() == "{\"matched\":true}");
  }

  SUBCASE("game with two players should store correctly and parse to json") {
    game g{vector<session_id>{8, 915}, 87};

    CHECK(g.session_list.size() == 2);
    CHECK(g.session_list == std::vector<session_id>{8, 915});
    CHECK(g.session == 87);
    CHECK(g.data.dump() == "{\"matched\":true}");
  }
}

TEST_CASE("matchmaking data match function should pair players") {
  using std::unordered_set;
  using std::unordered_map;
  using std::vector;

  using session_id = minimal_player_traits::id::session_id;
  using id_hash = minimal_player_traits::id::hash;
  using session_data = minimal_matchmaker::session_data;
  using game = minimal_matchmaker::game;

  minimal_matchmaker matchmaker;
  unordered_map<session_id, session_data, id_hash> session_map;
  unordered_set<session_id, id_hash> altered_sessions;

  SUBCASE("empty map should return empty list of games") {
    vector<game> games;
    matchmaker.match(games, session_map, altered_sessions);

    CHECK(games.size() == 0);
  }

  SUBCASE("map with two players should return one game") {
    session_map.emplace(std::make_pair(9, session_data{json{}}));
    session_map.emplace(std::make_pair(3241, session_data{json{}}));
    vector<game> games;
    matchmaker.match(games, session_map, altered_sessions);

    CHECK(games.size() == 1);

    unordered_set<session_id, id_hash> sessions;
    for(const game& g : games) {
      for(session_id sid : g.session_list) {
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
    vector<game> games;
    matchmaker.match(games, session_map, altered_sessions);

    CHECK(games.size() == 3);

    unordered_set<session_id, id_hash> sessions;
    for(const game& g : games) {
      for(session_id sid : g.session_list) {
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
    CHECK(matchmaker.can_match(session_map, altered_sessions) == false);
  }

  SUBCASE("we should not be able to match a session map with one session") {
    session_map.emplace(std::make_pair(9231, session_data{json{}}));

    CHECK(matchmaker.can_match(session_map, altered_sessions) == false);
  }

  SUBCASE("we should be able to match a session map with two sessions") {
    session_map.emplace(std::make_pair(17, session_data{json{}}));
    session_map.emplace(std::make_pair(2, session_data{json{}}));

    CHECK(matchmaker.can_match(session_map, altered_sessions) == true);
  }
}
