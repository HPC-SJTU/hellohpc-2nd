#include "../share/riichi_rules.hpp"

#include "../share/make_move.hpp"
#include "../share/response_priority.hpp"
#include "../share/win_points.hpp"
#include "test_support.hpp"

namespace {

Tile_Array hand(std::initializer_list<int> tiles) {
  Tile_Array result = {};
  for (const int tile : tiles) result[tile]++;
  return result;
}

Open_Meld_Elem concealed_kan(const int tile) {
  Open_Meld_Elem meld;
  meld.type = FT_CONCEALED_KAN;
  meld.consumed.assign(4, tile);
  return meld;
}

void expect(const char *name, const bool actual, const bool expected) {
  test_support::expect(name, actual == expected);
}

void expect_points(const char *name, const std::array<int, 4> &actual,
                   const std::array<int, 4> &expected) {
  test_support::expect(name, actual == expected);
}

Moves response(const EventType type, const int player) {
  Event event;
  event.type = type;
  event.player = player;
  return Moves(1, event);
}

void test_first_win_responder() {
  std::array<Moves, 4> candidates;
  for (int pid = 0; pid < 4; pid++) candidates[pid] = response(EventType::PASS, pid);

  candidates[1] = response(EventType::WIN, 1);
  candidates[2] = response(EventType::WIN, 2);
  candidates[3] = response(EventType::WIN, 3);
  expect("nearest WIN responder", first_win_responder(0, candidates) == 1, true);

  candidates[1] = response(EventType::PON, 1);
  expect("skip non-WIN response", first_win_responder(0, candidates) == 2, true);

  candidates[2] = response(EventType::OPEN_KAN, 2);
  expect("furthest WIN responder", first_win_responder(0, candidates) == 3, true);

  candidates[3] = response(EventType::CHII, 3);
  expect("no WIN responder", first_win_responder(0, candidates) == -1, true);

  candidates[0] = response(EventType::WIN, 0);
  candidates[2] = response(EventType::WIN, 2);
  candidates[3] = response(EventType::WIN, 3);
  expect("head-bump order wraps seats", first_win_responder(1, candidates) == 2, true);
}

bool legal_action(const Tile_Array &hand_before_draw, const int drawn_tile,
                  const Event &concealed_kan_action) {
  Moves record;
  Event round;
  round.type = EventType::ROUND;
  record.push_back(round);
  record.push_back(make_tsumo(0, drawn_tile));

  Game_State state;
  state.player_state[0].hand = hand_before_draw;
  state.player_state[0].hand[drawn_tile]++;
  state.player_state[0].riichi_accepted = true;
  return is_legal_concealed_kan(record, state, concealed_kan_action);
}

}  // namespace

int main() {
  test_first_win_responder();
  expect_points("points_move_win regression", points_move_win(1, 2, 3, 40, 0, 1, 2),
                {{0, 7500, -5500, 0}});

  const Open_Meld_Vector no_melds;

  expect("isolated suited triplet",
         is_legal_concealed_kan_after_riichi(hand({1, 1, 1, 2, 3, 4, 15, 16, 17, 27, 28, 29, 35}),
                                             no_melds, 1),
         true);
  expect("honor triplet",
         is_legal_concealed_kan_after_riichi(
             hand({2, 3, 4, 15, 16, 17, 27, 28, 29, 31, 31, 31, 35}), no_melds, 31),
         true);
  expect("three triplets as three sequences",
         is_legal_concealed_kan_after_riichi(hand({1, 1, 1, 2, 2, 2, 3, 3, 3, 14, 15, 16, 37}),
                                             no_melds, 1),
         false);
  expect("physical wait removed by fourth copy",
         is_legal_concealed_kan_after_riichi(hand({1, 2, 3, 3, 3, 14, 15, 16, 27, 28, 29, 31, 31}),
                                             no_melds, 3),
         false);
  expect("nine gates",
         is_legal_concealed_kan_after_riichi(hand({1, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 9}),
                                             no_melds, 1),
         false);
  expect("draw does not extend triplet",
         is_legal_concealed_kan_after_riichi(hand({1, 1, 1, 2, 3, 4, 15, 16, 17, 27, 28, 29, 35}),
                                             no_melds, 2),
         false);
  expect("red five in triplet",
         is_legal_concealed_kan_after_riichi(
             hand({5, 5, 10, 11, 12, 13, 14, 15, 16, 27, 28, 29, 31}), no_melds, 5),
         true);
  expect("red five draw",
         is_legal_concealed_kan_after_riichi(
             hand({5, 5, 5, 11, 12, 13, 14, 15, 16, 27, 28, 29, 31}), no_melds, 10),
         true);
  expect("forwarded honor kan",
         is_legal_concealed_kan_after_riichi(
             hand({2, 3, 4, 15, 16, 17, 27, 28, 29, 31, 31, 31, 31}), no_melds, 31),
         false);

  Open_Meld_Vector existing_kan(1, concealed_kan(18));
  expect("existing concealed kan",
         is_legal_concealed_kan_after_riichi(hand({1, 1, 1, 2, 3, 4, 25, 26, 27, 35}), existing_kan,
                                             1),
         true);
  expect("WRC theoretical fifth tile",
         is_legal_concealed_kan_after_riichi(hand({1, 2, 3, 4, 5, 6, 17, 17, 17, 19}), existing_kan,
                                             17),
         false);
  expect("four owned tiles have no completing tile",
         completing_tile_exists(hand({17}), existing_kan, 18), false);
  expect("three owned tiles have a completing tile",
         completing_tile_exists(hand({17}), existing_kan, 17), true);

  const Tile_Array isolated_triplet = hand({1, 1, 1, 2, 3, 4, 15, 16, 17, 27, 28, 29, 35});
  expect("legal action integration",
         legal_action(isolated_triplet, 1, make_concealed_kan_default(0, 1)), true);
  expect("action kind differs from draw",
         legal_action(hand({1, 1, 1, 1, 2, 3, 4, 15, 16, 17, 27, 28, 29}), 2,
                      make_concealed_kan_default(0, 1)),
         false);
  expect("red draw action integration",
         legal_action(hand({5, 5, 5, 11, 12, 13, 14, 15, 16, 27, 28, 29, 31}), 10,
                      make_concealed_kan_red(0, 5)),
         true);

  return 0;
}
