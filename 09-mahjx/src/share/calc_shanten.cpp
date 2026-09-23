#include "calc_shanten.hpp"

Tile_Array using_tile_array(const Tile_Array &hand, const Open_Meld_Vector &open_meld) {
  Tile_Array using_array = hand;
  for (int i = 0; i < open_meld.size(); i++) {
    if (open_meld[i].type != FT_CONCEALED_KAN) {
      using_array[open_meld[i].tile]++;
    }
    for (int j = 0; j < open_meld[i].consumed.size(); j++) {
      using_array[open_meld[i].consumed[j]]++;
    }
  }
  return using_array;
}

void tenpai_check(const int round_wind_tile, const int self_wind_tile,
                  const Tile_Array &using_array, const Tile_Array &hand, const Tile_Array &hand_cut,
                  Tile_Array &hand_tmp, const Open_Meld_Vector &open_meld,
                  Tenpai_Info &tenpai_info) {
  int rest = 0;
  for (int tile = 0; tile < 38; tile++) {
    rest = rest + hand_tmp[tile];
  }

  if (rest == 0) {
    // The input is already a meld-hand win shape. Do nothing for now.
  } else if (rest == 1) {
    for (int tile = 0; tile < 38; tile++) {
      if (hand_tmp[tile] == 1 && using_array[tile] != 4) {
        tenpai_info.meld_shanten_num = 0;
        tenpai_info.win_vec.push_back(calc_win(round_wind_tile, self_wind_tile, hand, hand_cut,
                                               hand_tmp, open_meld, tile, MT_PAIR_WAIT, false));
      }
    }
  } else if (rest == 2) {
    for (int tile = 0; tile < 38; tile++) {
      if (hand_tmp[tile] == 2 && using_array[tile] != 4) {
        tenpai_info.meld_shanten_num = 0;
        tenpai_info.win_vec.push_back(calc_win(round_wind_tile, self_wind_tile, hand, hand_cut,
                                               hand_tmp, open_meld, tile, MT_DUAL_PAIR, false));
      }
    }
    for (int tile = 0; tile < 30; tile++) {
      if (hand_tmp[tile] == 1 && hand_tmp[tile + 1] == 1) {
        if (tile % 10 == 1) {
          if (using_array[tile + 2] != 4) {
            tenpai_info.meld_shanten_num = 0;
            tenpai_info.win_vec.push_back(calc_win(round_wind_tile, self_wind_tile, hand, hand_cut,
                                                   hand_tmp, open_meld, tile + 2, MT_EDGE_WAIT,
                                                   false));
          }
        } else if (tile % 10 == 8) {
          if (using_array[tile - 1] != 4) {
            tenpai_info.meld_shanten_num = 0;
            tenpai_info.win_vec.push_back(calc_win(round_wind_tile, self_wind_tile, hand, hand_cut,
                                                   hand_tmp, open_meld, tile - 1, MT_EDGE_WAIT,
                                                   false));
          }
        } else {
          if (using_array[tile - 1] != 4) {
            tenpai_info.meld_shanten_num = 0;
            tenpai_info.win_vec.push_back(calc_win(round_wind_tile, self_wind_tile, hand, hand_cut,
                                                   hand_tmp, open_meld, tile - 1, MT_OPEN_WAIT,
                                                   false));
          }
          if (using_array[tile + 2] != 4) {
            tenpai_info.meld_shanten_num = 0;
            tenpai_info.win_vec.push_back(calc_win(round_wind_tile, self_wind_tile, hand, hand_cut,
                                                   hand_tmp, open_meld, tile + 2, MT_OPEN_WAIT,
                                                   false));
          }
        }
      }
      if (hand_tmp[tile] == 1 && hand_tmp[tile + 2] == 1) {
        if (tile % 10 != 9) {
          if (using_array[tile + 1] != 4) {
            tenpai_info.meld_shanten_num = 0;
            tenpai_info.win_vec.push_back(calc_win(round_wind_tile, self_wind_tile, hand, hand_cut,
                                                   hand_tmp, open_meld, tile + 1, MT_KANCHAN,
                                                   false));
          }
        }
      }
    }
  }
}

void analyze_proto_sequence(const int round_wind_tile, const int self_wind_tile,
                            const Tile_Array &using_array, const Tile_Array &hand,
                            const Tile_Array &hand_cut, Tile_Array &hand_tmp,
                            const Open_Meld_Vector &open_meld, Tenpai_Info &tenpai_info) {
  // Delete the obviously useless patterns from here. Do not process a hand_tmp that contains a
  // meld.
  for (int tile = 0; tile < 38; tile++) {
    if (hand_tmp[tile] >= 3) {
      return;
    }
  }

  for (int j = 0; j < 3; j++) {
    for (int i = 1; i <= 7; i++) {
      if (hand_tmp[10 * j + i] > 0 && hand_tmp[10 * j + i + 1] > 0 &&
          hand_tmp[10 * j + i + 2] > 0) {
        return;
      }
    }
  }
  // Delete the obviously useless patterns up to here.

  tenpai_check(round_wind_tile, self_wind_tile, using_array, hand, hand_cut, hand_tmp, open_meld,
               tenpai_info);
}

void cut_sequence(const int round_wind_tile, const int self_wind_tile,
                  const Tile_Array &using_array, const Tile_Array &hand, const Tile_Array &hand_cut,
                  Tile_Array &hand_tmp, const int start, const Open_Meld_Vector &open_meld,
                  Tenpai_Info &tenpai_info) {
  for (int tile = start; tile < 30; tile++) {
    if (hand_tmp[tile] >= 1 && hand_tmp[tile + 1] >= 1 && hand_tmp[tile + 2] >= 1) {
      hand_tmp[tile] = hand_tmp[tile] - 1;
      hand_tmp[tile + 1] = hand_tmp[tile + 1] - 1;
      hand_tmp[tile + 2] = hand_tmp[tile + 2] - 1;
      cut_sequence(round_wind_tile, self_wind_tile, using_array, hand, hand_cut, hand_tmp, tile,
                   open_meld, tenpai_info);
      hand_tmp[tile] = hand_tmp[tile] + 1;
      hand_tmp[tile + 1] = hand_tmp[tile + 1] + 1;
      hand_tmp[tile + 2] = hand_tmp[tile + 2] + 1;
    }
  }
  analyze_proto_sequence(round_wind_tile, self_wind_tile, using_array, hand, hand_cut, hand_tmp,
                         open_meld, tenpai_info);
}

void cut_triplet(const int round_wind_tile, const int self_wind_tile, const Tile_Array &using_array,
                 const Tile_Array &hand, Tile_Array &hand_tmp, const int start,
                 const Open_Meld_Vector &open_meld, Tenpai_Info &tenpai_info) {
  for (int tile = start; tile < 38; tile++) {
    if (hand_tmp[tile] >= 3) {
      hand_tmp[tile] = hand_tmp[tile] - 3;
      cut_triplet(round_wind_tile, self_wind_tile, using_array, hand, hand_tmp, tile, open_meld,
                  tenpai_info);
      hand_tmp[tile] = hand_tmp[tile] + 3;
    }
  }

  Tile_Array hand_cut = hand_tmp;
  cut_sequence(round_wind_tile, self_wind_tile, using_array, hand, hand_cut, hand_tmp, 0, open_meld,
               tenpai_info);
}

void seven_pairs_shanten(const int round_wind_tile, const int self_wind_tile,
                         const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                         Tenpai_Info &tenpai_info) {
  int head_num = 0;
  int isolated_tile_num = 0;
  for (int tile = 0; tile < 38; tile++) {
    if (hand[tile] >= 2) {
      head_num++;
    } else if (hand[tile] > 0) {
      isolated_tile_num++;
    }
  }
  if (head_num == 7) {
    // The input is already a seven-pairs win shape. Do nothing for now.
  } else if (head_num == 6) {
    for (int tile = 0; tile < 38; tile++) {
      if (hand[tile] == 1) {
        tenpai_info.seven_pairs_shanten_num = 0;
        tenpai_info.win_vec.push_back(calc_win(round_wind_tile, self_wind_tile, hand, hand, hand,
                                               open_meld, tile, MT_PAIR_WAIT, true));
      }
    }
  }

  int tmp = 13 - 2 * head_num - std::min(7 - head_num, isolated_tile_num);
  tenpai_info.seven_pairs_shanten_num = std::min(tmp, tenpai_info.seven_pairs_shanten_num);
}

void analyze_hand(const int round_wind_tile, const int self_wind_tile, const Tile_Array using_array,
                  const Tile_Array hand, const Open_Meld_Vector open_meld,
                  Tenpai_Info &tenpai_info) {
  // A hand that uses four tiles of the winning tile kind is not tenpai. Store the used tile count
  // of each kind in the array using_array. The hand and the open meld passed to this function must
  // contain no red tiles.
  Tile_Array hand_tmp = hand;
  for (int tile = 0; tile < 38; tile++) {
    if (hand[tile] >= 2) {
      hand_tmp[tile] = hand_tmp[tile] - 2;
      cut_triplet(round_wind_tile, self_wind_tile, using_array, hand, hand_tmp, 0, open_meld,
                  tenpai_info);
      hand_tmp[tile] = hand_tmp[tile] + 2;
    }
  }
  cut_triplet(round_wind_tile, self_wind_tile, using_array, hand, hand_tmp, 0, open_meld,
              tenpai_info);

  if (open_meld.size() == 0) {
    seven_pairs_shanten(round_wind_tile, self_wind_tile, hand, open_meld, tenpai_info);
  }
}

Tenpai_Info cal_tenpai_info(const int round_wind, const int self_wind, const Tile_Array &hand,
                            const Open_Meld_Vector &open_meld) {
  const int round_wind_tile = 31 + round_wind;
  const int self_wind_tile = 31 + self_wind;
  const Tile_Array hand_kind = tile_kind(hand);
  const Open_Meld_Vector open_meld_kind = tile_kind(open_meld);
  Tenpai_Info tenpai_info;

  analyze_hand(round_wind_tile, self_wind_tile, using_tile_array(hand_kind, open_meld_kind),
               hand_kind, open_meld_kind, tenpai_info);
  return tenpai_info;
}

int count_terminal_or_honor_kind(const Tile_Array &hand) {
  int terminal_or_honor_kind_num = 0;
  for (int c = 0; c < 3; c++) {
    if (hand[c * 10 + 1] > 0) {
      terminal_or_honor_kind_num++;
    }
    if (hand[c * 10 + 9] > 0) {
      terminal_or_honor_kind_num++;
    }
  }
  for (int tile = 31; tile < 38; tile++) {
    if (hand[tile] > 0) {
      terminal_or_honor_kind_num++;
    }
  }
  return terminal_or_honor_kind_num;
}
