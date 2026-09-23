#include "hand_group.hpp"

Open_Meld_Type open_meld_action_type_to_open_meld_type(const int open_meld_action_type) {
  if (11 <= open_meld_action_type && open_meld_action_type <= 13) {
    return FT_CHII;
  } else {
    return (Open_Meld_Type)open_meld_action_type;
  }
}

Hand_Location::Hand_Location() { data = 0; }

Hand_Location::Hand_Location(const int first, const int second) {
  assert(first < 256);
  assert(second < 256 * 256 * 256);
  data = (uint32_t(first) << 24) + uint32_t(second);
}

int Hand_Location::get_first() const { return int(data >> 24); }

int Hand_Location::get_second() const { return int(data & 0x00FFFFFF); }

int loc_intvec(const std::vector<int> &vec, const int num) {
  for (int i = 0; i < vec.size(); i++) {
    if (vec[i] == num) {
      return i;
    }
  }
  return -1;
}

Hand_Group::Hand_Group() { reset(); }

void Hand_Group::reset() {
  proto_sequence_loc.clear();
  hand_change.reset();
}

void Hand_Group::set_proto_sequence_value_init(
    const Bit_Tile_Num &hand_bit_original, const Hand_State2 &ts, const int open_meld_cand_tile,
    boost::unordered_map<Hand_State2, int> &ts_map,
    std::array<
        boost::container::static_vector<Hand_Analyzer_Basic, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
        CAL_NUM_THREAD> &cal_proto_sequence_value,
    const int thread_num) {
  proto_sequence_loc.clear();
  int red_in_choice[3] = {};
  int red_out_choice[3] = {};
  std::vector<int> tile_in_tmp, tile_out_tmp;

  for (int tile = 1; tile < 38; tile++) {
    if (tile % 10 != 0) {
      for (int i = 0;
           i <
           std::max(hand_change.hand_base.count_tile(tile) - hand_bit_original.count_tile(tile), 0);
           i++) {
        tile_in_tmp.push_back(tile);
      }
      for (int i = 0;
           i <
           std::max(hand_bit_original.count_tile(tile) - hand_change.hand_base.count_tile(tile), 0);
           i++) {
        tile_out_tmp.push_back(tile);
      }
    }
  }

  for (int hc = 0; hc < 3; hc++) {
    if (std::count(tile_in_tmp.begin(), tile_in_tmp.end(), hc * 10 + 5) != 0 &&
        open_meld_cand_tile == hc * 10 + 10) {
      red_in_choice[hc] = 2;
    } else {
      red_in_choice[hc] = 1;
    }
    if (ts.get_red_inside(hc) == 0 ||
        std::count(tile_out_tmp.begin(), tile_out_tmp.end(), hc * 10 + 5) == 0) {
      // There is no choice to discard a red five.
      red_out_choice[hc] = 1;
    } else if (hand_bit_original.count_tile(hc * 10 + 5) >
                   std::count(tile_out_tmp.begin(), tile_out_tmp.end(), hc * 10 + 5) &&
               ts.get_red_inside(hc) == 1) {
      // There are choices to discard a black five and a red five.
      red_out_choice[hc] = 2;
    } else if (hand_bit_original.count_tile(hc * 10 + 5) ==
                   std::count(tile_out_tmp.begin(), tile_out_tmp.end(), hc * 10 + 5) &&
               ts.get_red_inside(hc) == 1) {
      // The only choice is to discard a red five.
      red_out_choice[hc] = 1;
      tile_out_tmp[loc_intvec(tile_out_tmp, hc * 10 + 5)] = (hc + 1) * 10;
    } else {
      assert(false);
    }
  }

  for (int aic0 = 0; aic0 < red_in_choice[0]; aic0++) {
    if (aic0 == 1) {
      tile_in_tmp[loc_intvec(tile_in_tmp, 5)] = 10;
    }
    for (int aic1 = 0; aic1 < red_in_choice[1]; aic1++) {
      if (aic1 == 1) {
        tile_in_tmp[loc_intvec(tile_in_tmp, 15)] = 20;
      }
      for (int aic2 = 0; aic2 < red_in_choice[2]; aic2++) {
        if (aic2 == 1) {
          tile_in_tmp[loc_intvec(tile_in_tmp, 25)] = 30;
        }

        for (int aoc0 = 0; aoc0 < red_out_choice[0]; aoc0++) {
          if (aoc0 == 1) {
            tile_out_tmp[loc_intvec(tile_out_tmp, 5)] = 10;
          }
          for (int aoc1 = 0; aoc1 < red_out_choice[1]; aoc1++) {
            if (aoc1 == 1) {
              tile_out_tmp[loc_intvec(tile_out_tmp, 15)] = 20;
            }
            for (int aoc2 = 0; aoc2 < red_out_choice[2]; aoc2++) {
              if (aoc2 == 1) {
                tile_out_tmp[loc_intvec(tile_out_tmp, 25)] = 30;
              }
              Hand_Analyzer_Basic hand_analyzer;

              cal_proto_sequence_value[thread_num].push_back(hand_analyzer);
              const int loc_second_new = cal_proto_sequence_value[thread_num].size() - 1;
              cal_proto_sequence_value[thread_num][loc_second_new].reset_hand_analyzer_basic_with2(
                  hand_bit_original, ts);
              for (int i = 0; i < tile_in_tmp.size(); i++) {
                cal_proto_sequence_value[thread_num][loc_second_new].add_tile(tile_in_tmp[i]);
              }
              for (int i = 0; i < tile_out_tmp.size(); i++) {
                cal_proto_sequence_value[thread_num][loc_second_new].delete_tile(tile_out_tmp[i]);
              }
              proto_sequence_loc.push_back(Hand_Location(thread_num, loc_second_new));

              const int gn_new = proto_sequence_loc.size() - 1;
              ts_map[cal_proto_sequence_value[thread_num][loc_second_new].hand_state] = gn_new;

              if (aoc2 == 1) {
                tile_out_tmp[loc_intvec(tile_out_tmp, 30)] = 25;
              }
            }
            if (aoc1 == 1) {
              tile_out_tmp[loc_intvec(tile_out_tmp, 20)] = 15;
            }
          }
          if (aoc0 == 1) {
            tile_out_tmp[loc_intvec(tile_out_tmp, 10)] = 5;
          }
        }

        if (aic2 == 1) {
          tile_in_tmp[loc_intvec(tile_in_tmp, 30)] = 25;
        }
      }
      if (aic1 == 1) {
        tile_in_tmp[loc_intvec(tile_in_tmp, 20)] = 15;
      }
    }
    if (aic0 == 1) {
      tile_in_tmp[loc_intvec(tile_in_tmp, 10)] = 5;
    }
  }
}

void Hand_Group::add_open_meld_child(
    Hand_Analyzer_Basic &hand_analyzer, int open_meld_cand[38], const int open_meld_cand_copy[38],
    int open_meld_num_begin, int open_meld_num_end, int open_meld_must, int kan_cand[38],
    boost::unordered_map<Hand_State2, int> &ts_map, const int open_meld_type, int tile0, int tile1,
    int tile2, int tile3,
    std::array<
        boost::container::static_vector<Hand_Analyzer_Basic, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
        CAL_NUM_THREAD> &cal_proto_sequence_value,
    const int thread_num) {
  if (cal_proto_sequence_value[thread_num].size() >=
          MAX_PROTO_SEQUENCE_NUM_PER_THREAD - BUF_FOR_RIICHI_PER_THREAD ||
      proto_sequence_loc.size() >= MAX_PROTO_SEQUENCE_NUM_PER_GROUP - BUF_FOR_RIICHI_PER_GROUP) {
    // TODO: Report the proto-sequence capacity limit.
    return;
  }

  assert(open_meld_type != FT_CONCEALED_KAN);
  assert(open_meld_type != FT_OPEN_KAN);
  if (hand_analyzer.get_riichi() == 1 && open_meld_type != FT_CONCEALED_KAN) {
    return;
  }
  hand_analyzer.delete_tile(tile0);
  hand_analyzer.delete_tile(tile1);
  hand_analyzer.delete_tile(tile2);

  if ((tile0 % 10 == 0 && tile0 > 0) || (tile1 % 10 == 0 && tile1 > 0) ||
      (tile2 % 10 == 0 && tile2 > 0) || (tile3 % 10 == 0 && tile3 > 0)) {
    hand_analyzer.hand_state.set_red_inside((tile0 - 1) / 10, 0);
    hand_analyzer.hand_state.set_red_outside((tile0 - 1) / 10, 1);
  }
  hand_analyzer.hand_state.add_one_open_meld(
      open_meld_action_type_to_open_meld_type(open_meld_type),
      std::min({tile_kind(tile0), tile_kind(tile1), tile_kind(tile2)}));
  hand_analyzer.add_open_meld_num(1);
  if (hand_analyzer.get_open_meld_num() -
          cal_proto_sequence_value[proto_sequence_loc[0].get_first()]
                                  [proto_sequence_loc[0].get_second()]
                                      .get_open_meld_num() >=
      open_meld_num_begin) {
    if (ts_map.find(hand_analyzer.hand_state) == ts_map.end()) {
      ts_map[hand_analyzer.hand_state] = proto_sequence_loc.size();

      cal_proto_sequence_value[thread_num].push_back(hand_analyzer);
      const int loc_second_new = cal_proto_sequence_value[thread_num].size() - 1;
      proto_sequence_loc.push_back(Hand_Location(thread_num, loc_second_new));
    }
  }
  open_meld_cand[tile0]--;
  add_open_meld(hand_analyzer, open_meld_cand, open_meld_cand_copy, open_meld_num_begin,
                open_meld_num_end, open_meld_must, kan_cand, ts_map, cal_proto_sequence_value,
                thread_num);
  open_meld_cand[tile0]++;

  hand_analyzer.reduce_open_meld_num(1);
  hand_analyzer.hand_state.delete_one_open_meld(
      open_meld_action_type_to_open_meld_type(open_meld_type),
      std::min({tile_kind(tile0), tile_kind(tile1), tile_kind(tile2)}));
  if ((tile0 % 10 == 0 && tile0 > 0) || (tile1 % 10 == 0 && tile1 > 0) ||
      (tile2 % 10 == 0 && tile2 > 0) || (tile3 % 10 == 0 && tile3 > 0)) {
    hand_analyzer.hand_state.set_red_inside((tile0 - 1) / 10, 1);
    hand_analyzer.hand_state.set_red_outside((tile0 - 1) / 10, 0);
  }
  hand_analyzer.add_tile(tile0);
  hand_analyzer.add_tile(tile1);
  hand_analyzer.add_tile(tile2);
}

// TODO: Find why passing hand_analyzer by reference fails an assertion.
void Hand_Group::add_open_meld(
    Hand_Analyzer_Basic hand_analyzer, int open_meld_cand[38], const int open_meld_cand_copy[38],
    int open_meld_num_begin, int open_meld_num_end, int open_meld_must, int kan_cand[38],
    boost::unordered_map<Hand_State2, int> &ts_map,
    std::array<
        boost::container::static_vector<Hand_Analyzer_Basic, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
        CAL_NUM_THREAD> &cal_proto_sequence_value,
    const int thread_num) {
  if ((hand_analyzer.get_open_meld_num() - hand_analyzer.get_concealed_kan_num()) -
          (cal_proto_sequence_value[proto_sequence_loc[0].get_first()]
                                   [proto_sequence_loc[0].get_second()]
                                       .get_open_meld_num() -
           cal_proto_sequence_value[proto_sequence_loc[0].get_first()]
                                   [proto_sequence_loc[0].get_second()]
                                       .get_concealed_kan_num()) >=
      open_meld_num_end) {
    return;
  } else if (open_meld_must != 0 &&
             hand_analyzer.get_open_meld_num() -
                     cal_proto_sequence_value[proto_sequence_loc[0].get_first()]
                                             [proto_sequence_loc[0].get_second()]
                                                 .get_open_meld_num() >
                 0) {
    if (open_meld_cand[open_meld_must] == open_meld_cand_copy[open_meld_must]) {
      return;
    }
  }

  for (int tile = 0; tile < 38; tile++) {
    if (open_meld_cand[tile] > 0 && hand_analyzer.count_tile(tile) >= 3) {
      add_open_meld_child(hand_analyzer, open_meld_cand, open_meld_cand_copy, open_meld_num_begin,
                          open_meld_num_end, open_meld_must, kan_cand, ts_map, 2, tile, tile, tile,
                          0, cal_proto_sequence_value, thread_num);
    }
    if (open_meld_cand[tile] > 0 && tile % 10 == 5 && tile < 30 &&
        hand_analyzer.count_tile(tile) >= 2 && hand_analyzer.count_tile(tile + 5) == 1) {
      add_open_meld_child(hand_analyzer, open_meld_cand, open_meld_cand_copy, open_meld_num_begin,
                          open_meld_num_end, open_meld_must, kan_cand, ts_map, 2, tile, tile,
                          tile + 5, 0, cal_proto_sequence_value, thread_num);
    }
    if (open_meld_cand[tile] > 0 && tile % 10 == 0 && 0 < tile &&
        hand_analyzer.count_tile(tile) == 1 && hand_analyzer.count_tile(tile - 5) >= 2) {
      add_open_meld_child(hand_analyzer, open_meld_cand, open_meld_cand_copy, open_meld_num_begin,
                          open_meld_num_end, open_meld_must, kan_cand, ts_map, 2, tile, tile - 5,
                          tile - 5, 0, cal_proto_sequence_value, thread_num);
    }
  }

  int tile;
  for (int tile_c = 0; tile_c < 3; tile_c++) {
    for (int tile_n = 1; tile_n <= 7; tile_n++) {
      tile = tile_c * 10 + tile_n;
      if (open_meld_cand[tile] > 0 && hand_analyzer.count_tile(tile) > 0 &&
          hand_analyzer.count_tile(tile + 1) > 0 && hand_analyzer.count_tile(tile + 2) > 0) {
        add_open_meld_child(hand_analyzer, open_meld_cand, open_meld_cand_copy, open_meld_num_begin,
                            open_meld_num_end, open_meld_must, kan_cand, ts_map, 11, tile, tile + 1,
                            tile + 2, 0, cal_proto_sequence_value, thread_num);
      }
      if (open_meld_cand[tile] > 0 && tile % 10 == 3 && hand_analyzer.count_tile(tile) > 0 &&
          hand_analyzer.count_tile(tile + 1) > 0 && hand_analyzer.count_tile(tile + 7) > 0) {
        add_open_meld_child(hand_analyzer, open_meld_cand, open_meld_cand_copy, open_meld_num_begin,
                            open_meld_num_end, open_meld_must, kan_cand, ts_map, 11, tile, tile + 1,
                            tile + 7, 0, cal_proto_sequence_value, thread_num);
      }
      if (open_meld_cand[tile] > 0 && tile % 10 == 4 && hand_analyzer.count_tile(tile) > 0 &&
          hand_analyzer.count_tile(tile + 6) > 0 && hand_analyzer.count_tile(tile + 2) > 0) {
        add_open_meld_child(hand_analyzer, open_meld_cand, open_meld_cand_copy, open_meld_num_begin,
                            open_meld_num_end, open_meld_must, kan_cand, ts_map, 11, tile, tile + 6,
                            tile + 2, 0, cal_proto_sequence_value, thread_num);
      }
    }
    if (open_meld_cand[tile_c * 10 + 10] > 0 && hand_analyzer.count_tile(tile_c * 10 + 10) > 0 &&
        hand_analyzer.count_tile(tile_c * 10 + 6) > 0 &&
        hand_analyzer.count_tile(tile_c * 10 + 7) > 0) {
      add_open_meld_child(hand_analyzer, open_meld_cand, open_meld_cand_copy, open_meld_num_begin,
                          open_meld_num_end, open_meld_must, kan_cand, ts_map, 11, tile_c * 10 + 10,
                          tile_c * 10 + 6, tile_c * 10 + 7, 0, cal_proto_sequence_value,
                          thread_num);
    }
  }

  for (int tile_c = 0; tile_c < 3; tile_c++) {
    for (int tile_n = 2; tile_n <= 8; tile_n++) {
      tile = tile_c * 10 + tile_n;
      if (open_meld_cand[tile] > 0 && hand_analyzer.count_tile(tile) > 0 &&
          hand_analyzer.count_tile(tile - 1) > 0 && hand_analyzer.count_tile(tile + 1) > 0) {
        add_open_meld_child(hand_analyzer, open_meld_cand, open_meld_cand_copy, open_meld_num_begin,
                            open_meld_num_end, open_meld_must, kan_cand, ts_map, 12, tile, tile - 1,
                            tile + 1, 0, cal_proto_sequence_value, thread_num);
      }
      if (open_meld_cand[tile] > 0 && tile % 10 == 4 && hand_analyzer.count_tile(tile) > 0 &&
          hand_analyzer.count_tile(tile - 1) > 0 && hand_analyzer.count_tile(tile + 6) > 0) {
        add_open_meld_child(hand_analyzer, open_meld_cand, open_meld_cand_copy, open_meld_num_begin,
                            open_meld_num_end, open_meld_must, kan_cand, ts_map, 12, tile, tile - 1,
                            tile + 6, 0, cal_proto_sequence_value, thread_num);
      }
      if (open_meld_cand[tile] > 0 && tile % 10 == 6 && hand_analyzer.count_tile(tile) > 0 &&
          hand_analyzer.count_tile(tile + 4) > 0 && hand_analyzer.count_tile(tile + 1) > 0) {
        add_open_meld_child(hand_analyzer, open_meld_cand, open_meld_cand_copy, open_meld_num_begin,
                            open_meld_num_end, open_meld_must, kan_cand, ts_map, 12, tile, tile + 4,
                            tile + 1, 0, cal_proto_sequence_value, thread_num);
      }
    }
    if (open_meld_cand[tile_c * 10 + 10] > 0 && hand_analyzer.count_tile(tile_c * 10 + 10) > 0 &&
        hand_analyzer.count_tile(tile_c * 10 + 4) > 0 &&
        hand_analyzer.count_tile(tile_c * 10 + 6) > 0) {
      add_open_meld_child(hand_analyzer, open_meld_cand, open_meld_cand_copy, open_meld_num_begin,
                          open_meld_num_end, open_meld_must, kan_cand, ts_map, 12, tile_c * 10 + 10,
                          tile_c * 10 + 4, tile_c * 10 + 6, 0, cal_proto_sequence_value,
                          thread_num);
    }
  }

  for (int tile_c = 0; tile_c < 3; tile_c++) {
    for (int tile_n = 3; tile_n <= 9; tile_n++) {
      tile = tile_c * 10 + tile_n;
      if (open_meld_cand[tile] > 0 && hand_analyzer.count_tile(tile) > 0 &&
          hand_analyzer.count_tile(tile - 2) > 0 && hand_analyzer.count_tile(tile - 1) > 0) {
        add_open_meld_child(hand_analyzer, open_meld_cand, open_meld_cand_copy, open_meld_num_begin,
                            open_meld_num_end, open_meld_must, kan_cand, ts_map, 13, tile, tile - 2,
                            tile - 1, 0, cal_proto_sequence_value, thread_num);
      }
      if (open_meld_cand[tile] > 0 && tile % 10 == 6 && hand_analyzer.count_tile(tile) > 0 &&
          hand_analyzer.count_tile(tile - 2) > 0 && hand_analyzer.count_tile(tile + 4) > 0) {
        add_open_meld_child(hand_analyzer, open_meld_cand, open_meld_cand_copy, open_meld_num_begin,
                            open_meld_num_end, open_meld_must, kan_cand, ts_map, 13, tile, tile - 2,
                            tile + 4, 0, cal_proto_sequence_value, thread_num);
      }
      if (open_meld_cand[tile] > 0 && tile % 10 == 7 && hand_analyzer.count_tile(tile) > 0 &&
          hand_analyzer.count_tile(tile + 3) > 0 && hand_analyzer.count_tile(tile - 1) > 0) {
        add_open_meld_child(hand_analyzer, open_meld_cand, open_meld_cand_copy, open_meld_num_begin,
                            open_meld_num_end, open_meld_must, kan_cand, ts_map, 13, tile, tile + 3,
                            tile - 1, 0, cal_proto_sequence_value, thread_num);
      }
    }
    if (open_meld_cand[tile_c * 10 + 10] > 0 && hand_analyzer.count_tile(tile_c * 10 + 10) > 0 &&
        hand_analyzer.count_tile(tile_c * 10 + 3) > 0 &&
        hand_analyzer.count_tile(tile_c * 10 + 4) > 0) {
      add_open_meld_child(hand_analyzer, open_meld_cand, open_meld_cand_copy, open_meld_num_begin,
                          open_meld_num_end, open_meld_must, kan_cand, ts_map, 13, tile_c * 10 + 10,
                          tile_c * 10 + 3, tile_c * 10 + 4, 0, cal_proto_sequence_value,
                          thread_num);
    }
  }
}

void Hand_Group::add_concealed_kan(
    const int kan_cand[38], boost::unordered_map<Hand_State2, int> &ts_map,
    std::array<
        boost::container::static_vector<Hand_Analyzer_Basic, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
        CAL_NUM_THREAD> &cal_proto_sequence_value,
    const int thread_num) {
  const int proto_sequence_value_size_tmp = proto_sequence_loc.size();
  for (int gn = 0; gn < proto_sequence_value_size_tmp; gn++) {
    for (int tile = 1; tile < 38; tile++) {
      if (tile % 10 != 0 && kan_cand[tile] == 1) {
        // When we consider a hand derived by a kan, the source hand must hold exactly three tiles
        // of the target kind. If we replace a concealed triplet with a kan group in a hand with
        // four tiles, the hand ends with five tiles. This is impossible.
        const int proto_sequence_loc_gn_first = proto_sequence_loc[gn].get_first();
        const int proto_sequence_loc_gn_second = proto_sequence_loc[gn].get_second();
        if (cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                    .count_tile_kind(tile) == 3 &&
            cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                    .using_tile_kind_num(tile) == 3) {
          int tile2 = tile;
          if (cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                  .count_tile(tile) == 2) {
            assert(
                cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                    .hand_state.get_red_inside(tile / 10) == 1);
            tile2 += 5;
            // This handles the case where the original hand holds a red five. We can remove this
            // later because the best place for the red-tile handling is unclear. If the hand holds
            // three black fives, the concealed kan added here has no red five.
          }
          assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                     .hand_state.get_pon_num(tile) == 0);
          assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                     .hand_state.get_open_kan_num(tile) == 0);
          assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                     .hand_state.get_concealed_kan_num(tile) == 0);
          assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                     .get_open_meld_num() < 4);

          if (cal_proto_sequence_value[thread_num].size() >=
                  MAX_PROTO_SEQUENCE_NUM_PER_THREAD - BUF_FOR_RIICHI_PER_THREAD ||
              proto_sequence_loc.size() >=
                  MAX_PROTO_SEQUENCE_NUM_PER_GROUP - BUF_FOR_RIICHI_PER_GROUP) {
            // TODO: Report the concealed-kan capacity limit.
            return;
          }

          cal_proto_sequence_value[thread_num].push_back(
              cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]);
          const int loc_second_new = cal_proto_sequence_value[thread_num].size() - 1;
          cal_proto_sequence_value[thread_num][loc_second_new].delete_tile(tile);
          cal_proto_sequence_value[thread_num][loc_second_new].delete_tile(tile);
          cal_proto_sequence_value[thread_num][loc_second_new].delete_tile(tile2);
          cal_proto_sequence_value[thread_num][loc_second_new].add_concealed_kan(tile, tile2);
          cal_proto_sequence_value[thread_num][loc_second_new].set_kan_changed(1);
          proto_sequence_loc.push_back(Hand_Location(thread_num, loc_second_new));

          const int gn_new = proto_sequence_loc.size() - 1;
          assert(ts_map.find(cal_proto_sequence_value[thread_num][loc_second_new].hand_state) ==
                 ts_map.end());
          ts_map[cal_proto_sequence_value[thread_num][loc_second_new].hand_state] = gn_new;

          assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                     .hand_state.get_pon_num(tile) == 0);
          assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                     .hand_state.get_open_kan_num(tile) == 0);
          assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                     .hand_state.get_concealed_kan_num(tile) == 0);
          assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                     .get_open_meld_num() < 4);
        }
      }
    }
  }
}

void Hand_Group::add_open_kan(
    const int kan_cand[38], boost::unordered_map<Hand_State2, int> &ts_map,
    std::array<
        boost::container::static_vector<Hand_Analyzer_Basic, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
        CAL_NUM_THREAD> &cal_proto_sequence_value,
    const int thread_num) {
  const int proto_sequence_value_size_tmp = proto_sequence_loc.size();
  for (int gn = 0; gn < proto_sequence_value_size_tmp; gn++) {
    const int proto_sequence_loc_gn_first = proto_sequence_loc[gn].get_first();
    const int proto_sequence_loc_gn_second = proto_sequence_loc[gn].get_second();
    if (cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
            .get_riichi() == 1) {
      continue;
    }
    for (int tile = 1; tile < 38; tile++) {
      if (tile % 10 != 0 && kan_cand[tile] == 1) {
        // When we consider a hand derived by a kan, the source hand must hold exactly three tiles
        // of the target kind. If we replace a concealed triplet with a kan group in a hand with
        // four tiles, the hand ends with five tiles. This is impossible.
        if (cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                    .count_tile_kind(tile) == 3 &&
            cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                    .using_tile_kind_num(tile) == 3) {
          int tile2 = tile;
          if (cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                  .count_tile(tile) == 2) {
            assert(
                cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                    .hand_state.get_red_inside(tile / 10) == 1);
            tile2 += 5;
            // This handles the case where the original hand holds a red five. We can remove this
            // later because the best place for the red-tile handling is unclear. If the hand holds
            // three black fives, the open kan added here has no red five.
          }
          assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                     .hand_state.get_pon_num(tile) == 0);
          assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                     .hand_state.get_open_kan_num(tile) == 0);
          assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                     .hand_state.get_concealed_kan_num(tile) == 0);
          assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                     .get_open_meld_num() < 4);

          if (cal_proto_sequence_value[thread_num].size() >=
                  MAX_PROTO_SEQUENCE_NUM_PER_THREAD - BUF_FOR_RIICHI_PER_THREAD ||
              proto_sequence_loc.size() >=
                  MAX_PROTO_SEQUENCE_NUM_PER_GROUP - BUF_FOR_RIICHI_PER_GROUP) {
            // TODO: Report the open-kan capacity limit.
            return;
          }

          cal_proto_sequence_value[thread_num].push_back(
              cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]);
          const int loc_second_new = cal_proto_sequence_value[thread_num].size() - 1;
          cal_proto_sequence_value[thread_num][loc_second_new].delete_tile(tile);
          cal_proto_sequence_value[thread_num][loc_second_new].delete_tile(tile);
          cal_proto_sequence_value[thread_num][loc_second_new].delete_tile(tile2);
          cal_proto_sequence_value[thread_num][loc_second_new].add_open_kan(tile, tile2);
          cal_proto_sequence_value[thread_num][loc_second_new].set_kan_changed(1);
          proto_sequence_loc.push_back(Hand_Location(thread_num, loc_second_new));

          const int gn_new = proto_sequence_loc.size() - 1;
          assert(ts_map.find(cal_proto_sequence_value[thread_num][loc_second_new].hand_state) ==
                 ts_map.end());
          ts_map[cal_proto_sequence_value[thread_num][loc_second_new].hand_state] = gn_new;

          assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                     .hand_state.get_pon_num(tile) == 0);
          assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                     .hand_state.get_open_kan_num(tile) == 0);
          assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                     .hand_state.get_concealed_kan_num(tile) == 0);
          assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                     .get_open_meld_num() < 4);
        }
      }
    }
  }
}

void Hand_Group::add_upgraded_kan(
    const int kan_cand[38], boost::unordered_map<Hand_State2, int> &ts_map,
    std::array<
        boost::container::static_vector<Hand_Analyzer_Basic, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
        CAL_NUM_THREAD> &cal_proto_sequence_value,
    const int thread_num) {
  int proto_sequence_value_size_tmp = proto_sequence_loc.size();
  for (int gn = 0; gn < proto_sequence_value_size_tmp; gn++) {
    const int proto_sequence_loc_gn_first = proto_sequence_loc[gn].get_first();
    const int proto_sequence_loc_gn_second = proto_sequence_loc[gn].get_second();
    for (int tile = 1; tile < 38; tile++) {
      if (tile % 10 != 0 &&
          cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                  .hand_state.get_pon_num(tile) == 1 &&
          kan_cand[tile] == 1) {
        assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                   .hand_state.get_open_kan_num(tile) == 0);
        assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                   .hand_state.get_riichi() == 0);

        if (cal_proto_sequence_value[thread_num].size() >=
                MAX_PROTO_SEQUENCE_NUM_PER_THREAD - BUF_FOR_RIICHI_PER_THREAD ||
            proto_sequence_loc.size() >=
                MAX_PROTO_SEQUENCE_NUM_PER_GROUP - BUF_FOR_RIICHI_PER_GROUP) {
          // TODO: Report the upgraded-kan capacity limit.
          return;
        }

        if (cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                    .count_tile_kind(tile) == 0 &&
            cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                    .using_tile_kind_num(tile) == 3) {
          cal_proto_sequence_value[thread_num].push_back(
              cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]);
          const int loc_second_new = cal_proto_sequence_value[thread_num].size() - 1;
          cal_proto_sequence_value[thread_num][loc_second_new].change_pon_to_upgraded_kan(tile,
                                                                                          tile);
          cal_proto_sequence_value[thread_num][loc_second_new].set_kan_changed(1);
          proto_sequence_loc.push_back(Hand_Location(thread_num, loc_second_new));

          const int gn_new = proto_sequence_loc.size() - 1;
          assert(ts_map.find(cal_proto_sequence_value[thread_num][loc_second_new].hand_state) ==
                 ts_map.end());
          ts_map[cal_proto_sequence_value[thread_num][loc_second_new].hand_state] = gn_new;
        }
        assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                   .hand_state.get_pon_num(tile) == 1);
        assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                   .hand_state.get_open_kan_num(tile) == 0);
        assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                   .hand_state.get_riichi() == 0);
      }
    }
  }
}

void Hand_Group::analyze_all_tenpai(
    const int my_pid, const Game_State &game_state,
    std::array<
        boost::container::static_vector<Hand_Analyzer_Basic, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
        CAL_NUM_THREAD> &cal_proto_sequence_value) {
  for (int gn = 0; gn < proto_sequence_loc.size(); gn++) {
    cal_proto_sequence_value[proto_sequence_loc[gn].get_first()]
                            [proto_sequence_loc[gn].get_second()]
                                .analyze_tenpai(my_pid, game_state);
  }
}

void Hand_Group::analyze_all_win(
    const int my_pid, const Game_State &game_state,
    std::array<
        boost::container::static_vector<Hand_Analyzer_Basic, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
        CAL_NUM_THREAD> &cal_proto_sequence_value,
    boost::container::static_vector<Win_Calc, MAX_WIN_NUM_PER_THREAD> &win_graph,
    std::array<std::array<std::array<int, 3>, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>, CAL_NUM_THREAD>
        &win_graph_loc,
    const int thread_num) {
  for (int gn = 0; gn < proto_sequence_loc.size(); gn++) {
    const int proto_sequence_loc_gn_first = proto_sequence_loc[gn].get_first();
    const int proto_sequence_loc_gn_second = proto_sequence_loc[gn].get_second();
    win_graph_loc[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second][0] = thread_num;
    win_graph_loc[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second][1] = win_graph.size();
    cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
        .analyze_hand(my_pid, game_state, win_graph);
    win_graph_loc[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second][2] = win_graph.size();
  }
}

void Hand_Group::add_riichi(
    boost::unordered_map<Hand_State2, int> &ts_map,
    std::array<
        boost::container::static_vector<Hand_Analyzer_Basic, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
        CAL_NUM_THREAD> &cal_proto_sequence_value,
    const int thread_num) {
  int proto_sequence_value_size_tmp = proto_sequence_loc.size();
  for (int gn = 0; gn < proto_sequence_value_size_tmp; gn++) {
    const int proto_sequence_loc_gn_first = proto_sequence_loc[gn].get_first();
    const int proto_sequence_loc_gn_second = proto_sequence_loc[gn].get_second();
    if (cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                .get_tenpai() == 1 &&
        cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                    .get_open_meld_num() -
                cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                    .get_concealed_kan_num() ==
            0) {
      assert(cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]
                 .get_riichi() == 0);

      if (cal_proto_sequence_value[thread_num].size() >= MAX_PROTO_SEQUENCE_NUM_PER_THREAD ||
          proto_sequence_loc.size() >= MAX_PROTO_SEQUENCE_NUM_PER_GROUP) {
        // TODO: Report the riichi capacity limit.
        return;
      }

      cal_proto_sequence_value[thread_num].push_back(
          cal_proto_sequence_value[proto_sequence_loc_gn_first][proto_sequence_loc_gn_second]);
      const int loc_second_new = cal_proto_sequence_value[thread_num].size() - 1;
      cal_proto_sequence_value[thread_num][loc_second_new].set_riichi(1);
      proto_sequence_loc.push_back(Hand_Location(thread_num, loc_second_new));

      const int gn_new = proto_sequence_loc.size() - 1;
      ts_map[cal_proto_sequence_value[thread_num][loc_second_new].hand_state] = gn_new;
    }
  }
}
