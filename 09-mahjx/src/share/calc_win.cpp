#include "calc_win.hpp"

Win_Info calc_win(const int round_wind_tile, const int self_wind_tile, const Tile_Array &hand,
                  const Tile_Array &hand_cut, const Tile_Array &hand_tmp,
                  const Open_Meld_Vector &open_meld, const int wait_tile, const Wait_Type wait_type,
                  const bool seven_pairs) {
  Tile_Array hand_rest;
  for (int tile = 0; tile < 38; tile++) {
    hand_rest[tile] = hand[tile] - hand_cut[tile];
  }

  Win_Info win_info;
  win_info.tile = wait_tile;

  int pure_double_sequence_num, kan_num;
  bool closed_hand, has_round_wind, has_self_wind, has_dragon[3], has_wind[4], pinfu;
  bool full_flush, half_flush, simples, pure_outside_hand, outside_hand, all_triplets;
  bool pure_double_sequence, double_pure_double_sequence, kan3, kan4, three_color_sequence,
      three_color_triplet, full_straight;
  bool big_three_dragons, little_three_dragons, all_terminals, terminals_and_honors, nine_gates,
      suitsu, big_four_winds, little_four_winds;

  closed_hand = closed_hand_check(open_meld);
  full_flush = full_flush_check(hand, open_meld);
  half_flush = full_flush ? false : half_flush_check(hand, open_meld);
  simples = simples_check(hand, open_meld, wait_tile);
  terminals_and_honors = terminals_and_honors_check(hand, open_meld, wait_tile);
  suitsu = suitsu_check(hand, open_meld);
  int concealed_triplet_3 =
      0;  // The concealed triplet yaku can differ between tsumo and ron. Keep the flag as an
          // integer. 0: not a yaku, 1: a yaku only on tsumo, 2: always a yaku.
  int concealed_triplet_4 = 0;

  if (seven_pairs) {
    has_round_wind = false;
    has_self_wind = false;
    has_dragon[0] = false;
    has_dragon[1] = false;
    has_dragon[2] = false;
    pinfu = false;
    pure_outside_hand = false;
    outside_hand = false;
    all_triplets = false;
    pure_double_sequence = false;
    double_pure_double_sequence = false;
    three_color_sequence = false;
    three_color_triplet = false;
    full_straight = false;
    kan3 = false;
    kan4 = false;
    big_three_dragons = false;
    little_three_dragons = false;
    all_terminals = false;
    nine_gates = false;
    little_four_winds = false;
    big_four_winds = false;
  } else {
    for (int i = 0; i < 3; i++) {
      has_dragon[i] = value_tile_check(35 + i, hand_rest, open_meld, wait_tile, wait_type);
    }
    for (int i = 0; i < 4; i++) {
      has_wind[i] = value_tile_check(31 + i, hand_rest, open_meld, wait_tile, wait_type);
    }
    has_round_wind = has_wind[round_wind_tile - 31];
    has_self_wind = has_wind[self_wind_tile - 31];
    pinfu = pinfu_check(hand_rest, open_meld, wait_type, round_wind_tile, self_wind_tile);

    pure_outside_hand = pure_outside_hand_check(hand, hand_rest, open_meld, hand_tmp, wait_tile);
    outside_hand = pure_outside_hand
                       ? false
                       : outside_hand_check(hand, hand_rest, open_meld, hand_tmp, wait_tile);

    all_triplets = all_triplets_check(hand_cut, open_meld, hand_tmp, wait_type);

    pure_double_sequence_num =
        count_pure_double_sequences(hand_cut, open_meld, hand_tmp, wait_tile, wait_type);
    double_pure_double_sequence = (pure_double_sequence_num == 2);
    pure_double_sequence = (pure_double_sequence_num == 1);

    three_color_sequence =
        three_color_sequence_check(hand_cut, open_meld, hand_tmp, wait_tile, wait_type);
    three_color_triplet = three_color_triplet_check(hand_rest, open_meld, wait_tile, wait_type);
    full_straight = full_straight_check(hand_cut, open_meld, hand_tmp, wait_tile, wait_type);

    const int concealed_triplet_num = concealed_triplet_num_count(hand_rest, open_meld);
    if (concealed_triplet_num == 2) {
      if (wait_type == MT_DUAL_PAIR) {
        concealed_triplet_3 = 1;
      }
    } else if (concealed_triplet_num == 3) {
      concealed_triplet_3 = 2;
      if (wait_type == MT_DUAL_PAIR) {
        concealed_triplet_4 = 1;
      }
    } else if (concealed_triplet_num == 4) {
      concealed_triplet_4 = 2;
    }

    kan_num = kan_num_count(open_meld);
    kan3 = (kan_num == 3);
    kan4 = (kan_num == 4);

    big_three_dragons = false;
    little_three_dragons = false;
    if (has_dragon[0] && has_dragon[1] && has_dragon[2]) {
      big_three_dragons = true;
    } else {
      for (int i = 0; i < 3; i++) {
        if (has_dragon[(i + 1) % 3] && has_dragon[(i + 2) % 3] &&
            (hand_rest[35 + i] == 2 || wait_tile == 35 + i)) {
          little_three_dragons = true;
        }
      }
    }

    big_four_winds = false;
    little_four_winds = false;
    if (has_wind[0] && has_wind[1] && has_wind[2] && has_wind[3]) {
      big_four_winds = true;
    } else {
      for (int i = 0; i < 4; i++) {
        if (has_wind[(i + 1) % 4] && has_wind[(i + 2) % 4] && has_wind[(i + 3) % 4] &&
            (hand_rest[31 + i] == 2 || wait_tile == 31 + i)) {
          little_four_winds = true;
        }
      }
    }

    nine_gates = full_flush && nine_gates_check(hand, open_meld, wait_tile);
    all_terminals = pure_outside_hand && all_triplets;
    if (all_terminals) {
      terminals_and_honors = false;
    }
  }

  int han_tsumo = 0;
  int han_ron = 0;
  if (all_terminals || big_three_dragons || kan4 || nine_gates || suitsu || big_four_winds ||
      little_four_winds || concealed_triplet_4 == 2) {
    han_tsumo = 13;
    han_ron = 13;
  } else {
    if (has_round_wind) {
      han_tsumo++;
    }
    if (has_self_wind) {
      han_tsumo++;
    }
    if (has_dragon[0]) {
      han_tsumo++;
    }
    if (has_dragon[1]) {
      han_tsumo++;
    }
    if (has_dragon[2]) {
      han_tsumo++;
    }
    if (pinfu) {
      han_tsumo++;
    }
    if (seven_pairs) {
      han_tsumo += 2;
    }
    if (simples) {
      han_tsumo++;
    }
    if (all_triplets) {
      han_tsumo += 2;
    }
    if (pure_double_sequence) {
      han_tsumo++;
    }
    if (double_pure_double_sequence) {
      han_tsumo += 3;
    }
    if (little_three_dragons) {
      han_tsumo += 2;
    }
    if (terminals_and_honors) {
      han_tsumo += 2;
    }
    if (kan3) {
      han_tsumo += 2;
    }
    if (three_color_triplet) {
      han_tsumo += 2;
    }
    if (full_flush) {
      han_tsumo += closed_hand ? 6 : 5;
    }
    if (half_flush) {
      han_tsumo += closed_hand ? 3 : 2;
    }
    if (pure_outside_hand) {
      han_tsumo += closed_hand ? 3 : 2;
    }
    if (outside_hand) {
      han_tsumo += closed_hand ? 2 : 1;
    }
    if (three_color_sequence) {
      han_tsumo += closed_hand ? 2 : 1;
    }
    if (full_straight) {
      han_tsumo += closed_hand ? 2 : 1;
    }
    if (concealed_triplet_3 == 2) {
      han_tsumo += 2;
    }

    han_ron = han_tsumo;
    if (concealed_triplet_4 == 1) {
      han_tsumo = 13;
    } else if (concealed_triplet_3 == 1) {
      han_tsumo += 2;
    }
    if (closed_hand) {
      han_tsumo++;
    }
  }

  win_info.han_tsumo = han_tsumo;
  win_info.han_ron = han_ron;

  std::pair<int, int> fu_pair =
      calc_fu(round_wind_tile, self_wind_tile, hand, hand_cut, hand_rest, hand_tmp, open_meld,
              wait_tile, wait_type, closed_hand, pinfu, seven_pairs);
  win_info.fu_tsumo = fu_pair.first;
  win_info.fu_ron = fu_pair.second;
  return win_info;
}
