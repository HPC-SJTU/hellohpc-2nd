#include "hand_calculator.hpp"

#include "simple_dp_sparsemax.hpp"

extern const bool console_out;
extern std::mutex mtx;

extern Hand_Calculator_Work hand_calculator_work;

Hand_Calculator::Hand_Calculator() { reset(0, true); }

Hand_Calculator::Hand_Calculator(int my_pid_in) { reset(my_pid_in, true); }

void Hand_Calculator::reset(int my_pid_in, bool has_red_dora_in) {
  my_pid = my_pid_in;
  has_red_dora = has_red_dora_in;
  for (int tile = 0; tile < 38; tile++) {
    tiles_in[tile] = 0;
    tiles_out[tile] = 0;
    effective[tile] = 0;
  }
  hand_all_num = 0;
  open_meld_cand_tile = 0;
  cnm_restrict = 0;
  rn_para = 1.0;
  cnm_restriction.erase(cnm_restriction.begin(), cnm_restriction.end());
  hand_calculator_work.candidates_work.clear();
  candidates_sub.clear();
  tcs.clear();
  tcs_sub.clear();

  for (int i = 0; i < CAL_NUM_THREAD; ++i) {
    hand_calculator_work.cal_proto_sequence_value_work[i].clear();
    hand_calculator_work.win_graph_work[i].clear();
  }
}

const Hand_Analyzer_Basic &Hand_Calculator::get_const_proto_sequence(
    const Hand_Location loc) const {
  return hand_calculator_work.cal_proto_sequence_value_work[loc.get_first()][loc.get_second()];
}

const Hand_Analyzer_Basic &Hand_Calculator::get_const_proto_sequence_cgn(const int cn,
                                                                         const int gn) const {
  return get_const_proto_sequence(hand_calculator_work.candidates_work[cn].proto_sequence_loc[gn]);
}

Hand_Analyzer_Basic &Hand_Calculator::get_ref_proto_sequence_cgn(const int cn, const int gn) {
  return hand_calculator_work.cal_proto_sequence_value_work
      [hand_calculator_work.candidates_work[cn].proto_sequence_loc[gn].get_first()]
      [hand_calculator_work.candidates_work[cn].proto_sequence_loc[gn].get_second()];
}

size_t Hand_Calculator::candidates_size() const {
  return hand_calculator_work.candidates_work.size();
}

size_t Hand_Calculator::group_size(const int cn) const {
  return hand_calculator_work.candidates_work[cn].proto_sequence_loc.size();
}

double Hand_Calculator::get_win_prob(const int cn, const int gn, const int tn) const {
  const Hand_Location &loc = hand_calculator_work.candidates_work[cn].proto_sequence_loc[gn];
  return hand_calculator_work.win_prob_work[loc.get_first()][loc.get_second()][mod2(tn)];
}

std::array<double, 5> Hand_Calculator::get_win_han_prob(const int cn, const int gn,
                                                        const int tn) const {
  const Hand_Location &loc = hand_calculator_work.candidates_work[cn].proto_sequence_loc[gn];
  return hand_calculator_work.win_han_prob_work[loc.get_first()][loc.get_second()][mod2(tn)];
}

double Hand_Calculator::get_win_exp(const int cn, const int gn, const int tn) const {
  const Hand_Location &loc = hand_calculator_work.candidates_work[cn].proto_sequence_loc[gn];
  return hand_calculator_work.win_exp_work[loc.get_first()][loc.get_second()][mod2(tn)];
}

double Hand_Calculator::get_tenpai_prob(const int cn, const int gn, const int tn) const {
  const Hand_Location &loc = hand_calculator_work.candidates_work[cn].proto_sequence_loc[gn];
  return hand_calculator_work.tenpai_prob_work[loc.get_first()][loc.get_second()][mod2(tn)];
}

double Hand_Calculator::get_points_exp(const int cn, const int gn, const int tn) const {
  const Hand_Location &loc = hand_calculator_work.candidates_work[cn].proto_sequence_loc[gn];
  return hand_calculator_work.points_exp_work[loc.get_first()][loc.get_second()][mod2(tn)];
}

double Hand_Calculator::get_points_gain(const int cn, const int gn, const int tn) const {
  const Hand_Location &loc = hand_calculator_work.candidates_work[cn].proto_sequence_loc[gn];
  return hand_calculator_work.points_exp_work[loc.get_first()][loc.get_second()][mod2(tn)];
}

double Hand_Calculator::get_fold_exp(const int cn, const int gn, const int tn) const {
  const Hand_Location &loc = hand_calculator_work.candidates_work[cn].proto_sequence_loc[gn];
  return hand_calculator_work.fold_exp_work[loc.get_first()][loc.get_second()];
}

void Hand_Calculator::get_effective(const Game_State &game_state, Hand_Analyzer hand_analyzer) {
  Hand_Analyzer proto_sequence_tmp = hand_analyzer;
  proto_sequence_tmp.pattern = 0;

  if (console_out) {
    std::cout << "shanten_num:" << hand_analyzer.get_shanten_num()
              << " meld_shanten_num:" << hand_analyzer.get_meld_shanten_num()
              << " seven_pairs_shanten_num:" << hand_analyzer.get_seven_pairs_shanten_num()
              << std::endl;
  }
  if (hand_analyzer.get_shanten_num() > 0) {
    if (console_out) {
      std::cout << "effective:";
    }
    for (int tile = 1; tile < 38; tile++) {
      if (tile % 10 != 0) {
        proto_sequence_tmp.add_tile(tile);
        proto_sequence_tmp.analyze_tenpai(my_pid, game_state);
        if (proto_sequence_tmp.get_shanten_num() < hand_analyzer.get_shanten_num()) {
          effective[tile] = 1;
          if (console_out) {
            std::cout << " " << tile;
          }  // << ":" << proto_sequence_tmp.get_shanten_num();
          if (tile < 30 && tile % 10 == 5) {
            effective[tile + 5] = 1;
          }
        }
        proto_sequence_tmp.delete_tile(tile);
      }
    }
    if (open_meld_cand_tile != 0) {
      effective[open_meld_cand_tile] = 1;
    }
    if (console_out) {
      std::cout << std::endl;
    }
  }
}

void Hand_Calculator::set_in_out(Hand_Analyzer proto_sequence_in) {
  if (proto_sequence_in.get_hand_num() + proto_sequence_in.get_open_meld_num() * 3 == 14) {
    for (int tile = 0; tile < 38; tile++) {
      if (tile % 10 != 0 && proto_sequence_in.count_tile_kind(tile) > 0) {
        tiles_out[tile] = 1;
      }
    }
  } else {
    for (int tile = 1; tile < 38; tile++) {
      if (tile % 10 != 0) {
        tiles_in[tile] = 1;
      }
      if (tile % 10 != 0 && proto_sequence_in.count_tile_kind(tile) > 0) {
        tiles_out[tile] = 1;
      }
    }
  }

  for (int shanten_num = 0; shanten_num < 9; shanten_num++) {
    for (int niop = 0; niop < proto_sequence_in.inout_pattern_vec[shanten_num].size(); niop++) {
      for (int nin = 0;
           nin < proto_sequence_in.inout_pattern_vec[shanten_num][niop].tile_in_pattern.size();
           nin++) {
        tiles_in[proto_sequence_in.inout_pattern_vec[shanten_num][niop].tile_in_pattern[nin]] = 1;
      }
      for (int nout = 0;
           nout < proto_sequence_in.inout_pattern_vec[shanten_num][niop].tile_out_pattern.size();
           nout++) {
        tiles_out[proto_sequence_in.inout_pattern_vec[shanten_num][niop].tile_out_pattern[nout]] =
            1;
      }
    }
  }
  for (int shanten_num = 0; shanten_num < 7; shanten_num++) {
    for (int ntp = 0; ntp < proto_sequence_in.pattern_seven_pairs_vec[shanten_num].size(); ntp++) {
      for (int nip = 0;
           nip < proto_sequence_in.pattern_seven_pairs_vec[shanten_num][ntp].tile_in_pattern.size();
           nip++) {
        for (int nin = 0; nin < proto_sequence_in.pattern_seven_pairs_vec[shanten_num][ntp]
                                    .tile_in_pattern[nip]
                                    .size();
             nin++) {
          tiles_in[proto_sequence_in.pattern_seven_pairs_vec[shanten_num][ntp]
                       .tile_in_pattern[nip][nin]] = 1;
        }
      }
      for (int nout = 0;
           nout < proto_sequence_in.pattern_seven_pairs_vec[shanten_num][ntp].remain.size();
           nout++) {
        tiles_out[proto_sequence_in.pattern_seven_pairs_vec[shanten_num][ntp].remain[nout]] = 1;
      }
    }
  }
}

void Hand_Calculator::merge_candidates_child(std::vector<Hand_Group> &cand1,
                                             boost::unordered_set<Hand_Change> &tcs1,
                                             std::vector<Hand_Group> &cand2) {
  std::pair<boost::unordered_set<Hand_Change>::iterator, bool> ret;
  for (int i = 0; i < cand2.size(); i++) {
    cand2[i].hand_change.id = cand1.size();
    ret = tcs1.insert(cand2[i].hand_change);
    if (ret.second) {
      cand1.push_back(cand2[i]);
    }
  }
}

void Hand_Calculator::merge_candidates(std::vector<std::vector<Hand_Group>> &candvv,
                                       std::vector<boost::unordered_set<Hand_Change>> &tcsv) {
  int step = 1;
  while (step < candvv.size()) {
    for (int i = 0; i < candvv.size(); i += step * 2) {
      if (i + step < candvv.size()) {
        merge_candidates_child(candvv[i], tcsv[i], candvv[i + step]);
      }
    }
    step = step * 2;
  }
}

void Hand_Calculator::set_tsumo_node_1out(const Hand_Analyzer &proto_sequence_in) {
  Hand_Change hand_change_tmp_new;
  hand_change_tmp_new.hand_base = bit_tile_num;
  for (int tile = 0; tile < 38; tile++) {
    if (tile % 10 != 0 && proto_sequence_in.count_tile_kind(tile)) {
      hand_change_tmp_new.hand_base.delete_tile(tile);
      set_tsumo_node_exec(hand_change_tmp_new, true, hand_calculator_work.candidates_work, tcs,
                          in0num);
      hand_change_tmp_new.hand_base.add_tile(tile);
    }
  }
}

void Hand_Calculator::set_tsumo_node_1in_1out(const Game_State &game_state,
                                              const Hand_Analyzer_Basic &proto_sequence_in,
                                              Hand_Change &hand_change_tmp_new) {
  set_tsumo_node_exec(hand_change_tmp_new, true, hand_calculator_work.candidates_work, tcs, in0num);
  if (!game_state.player_state[my_pid].riichi_declared) {
    for (int tile_in = 0; tile_in < 38; tile_in++) {
      if (tile_in % 10 != 0 && proto_sequence_in.using_tile_kind_num(tile_in) < 4) {
        hand_change_tmp_new.hand_base.add_tile(tile_in);
        for (int tile_out = 0; tile_out < 38; tile_out++) {
          if (tile_out % 10 != 0 && proto_sequence_in.count_tile_kind(tile_out)) {
            hand_change_tmp_new.hand_base.delete_tile(tile_out);
            set_tsumo_node_exec(hand_change_tmp_new, false, hand_calculator_work.candidates_work,
                                tcs, in0num);
            hand_change_tmp_new.hand_base.add_tile(tile_out);
          }
        }
        hand_change_tmp_new.hand_base.delete_tile(tile_in);
      }
    }
  }
}

void Hand_Calculator::set_tsumo_node_seven_pairs(Hand_Analyzer proto_sequence_in, int in_num_end) {
  for (int nin = 1; nin <= in_num_end; nin++) {
    for (int ntp = 0; ntp < proto_sequence_in.pattern_seven_pairs_vec[in_num_end].size(); ntp++) {
      for (int nip = 0;
           nip < proto_sequence_in.pattern_seven_pairs_vec[in_num_end][ntp].tile_in_pattern.size();
           nip++) {
        set_tsumo_node_child(
            bit_tile_num, hand_all_num,
            proto_sequence_in.pattern_seven_pairs_vec[in_num_end][ntp].tile_in_pattern[nip],
            proto_sequence_in.pattern_seven_pairs_vec[in_num_end][ntp].remain, nin,
            hand_calculator_work.candidates_work, tcs, in0num);
      }
    }
  }
}

void Hand_Calculator::set_tsumo_edge(Hand_Change hand_change_tmp, const int thread_num) {
  for (int tile_out = 0; tile_out < 38; tile_out++) {
    if (tile_out % 10 != 0 && hand_change_tmp.hand_base.count_tile(tile_out) > 0) {
      hand_change_tmp.hand_base.delete_tile(tile_out);
      for (int tile_in = tile_out + 1; tile_in < 38; tile_in++) {
        if (tile_in % 10 != 0) {
          // The condition below means this. A tile that the root node cannot obtain never enters
          // the current hand. If the tile enters, the destination hand is not in the search hand
          // set.
          const bool admitted =
              tiles_in[tile_in] == 1 ||
              bit_tile_num.count_tile(tile_in) - hand_change_tmp.hand_base.count_tile(tile_in);
          if (admitted) {
            hand_change_tmp.hand_base.add_tile(tile_in);
            boost::unordered_set<Hand_Change>::iterator itr;
            itr = tcs.find(hand_change_tmp);
            if (itr != tcs.end()) {
              if (hand_calculator_work.tsumo_edge_work[thread_num].size() <
                  MAX_TSUMO_EDGE_NUM_PER_THREAD) {
                hand_calculator_work.tsumo_edge_work[thread_num].push_back(
                    Tsumo_Edge(hand_change_tmp.id, tile_in, tile_out, (*itr).id));
                // Register only one direction here. The reversed direction is considered later.
              } else {
                // TODO: Report the tsumo-edge capacity limit.
              }
            }
            hand_change_tmp.hand_base.delete_tile(tile_in);
          }
        }
      }
      hand_change_tmp.hand_base.add_tile(tile_out);
    }
  }
}

void Hand_Calculator::add_open_meld_node(const Game_State &game_state, int cn_start, int cn_end,
                                         int open_meld_num_begin, int open_meld_num_end,
                                         int must_effective, int open_meld_must,
                                         boost::unordered_map<Hand_State2, int> *ts_maps) {
  int kan_cand[38] = {};
  for (int tile = 1; tile < 38; tile++) {
    if (game_state.player_state[my_pid].hand[tile] == 4 ||
        (tile < 30 && tile % 10 == 5 && game_state.player_state[my_pid].hand[tile] == 3 &&
         game_state.player_state[my_pid].hand[tile + 5] == 1)) {
      kan_cand[tile] = 1;
    }
  }
  if (open_meld_must != 0) {
    kan_cand[tile_kind(open_meld_must)] = 1;
  }
  for (int fn = 0; fn < game_state.player_state[my_pid].open_meld.size(); fn++) {
    if (game_state.player_state[my_pid].open_meld[fn].type == FT_PON) {
      int pon_tile = tile_kind(game_state.player_state[my_pid].open_meld[fn].tile);
      if (game_state.player_state[my_pid].hand[pon_tile] == 1 ||
          pon_tile < 30 && pon_tile % 10 == 5 &&
              game_state.player_state[my_pid].hand[pon_tile + 5] == 1) {
        kan_cand[pon_tile] = 1;
      }
    }
  }
#pragma omp parallel for
  for (int cn = cn_start; cn < cn_end; cn++) {
    int open_meld_cand[38] = {};
    for (int tile = 0; tile < 38; tile++) {
      if (tile % 10 != 0) {
        open_meld_cand[tile] = std::max(
            hand_calculator_work.candidates_work[cn].hand_change.hand_base.count_tile(tile) -
                bit_tile_num.count_tile(tile),
            0);
      }
    }
    if (must_effective == 1) {
      for (int tile = 1; tile < 38; tile++) {
        open_meld_cand[tile] *= effective[tile];
      }
    }
    if (open_meld_must != 0) {
      open_meld_cand[open_meld_must]++;
    }
    if (open_meld_cand_tile != 0 && open_meld_cand_tile % 10 == 0) {
      open_meld_cand[open_meld_cand_tile] = 1;
    }

    int open_meld_cand_copy[38] = {};
    for (int tile = 0; tile < 38; tile++) {
      open_meld_cand_copy[tile] = open_meld_cand[tile];
    }
    int proto_sequence_value_size_tmp = group_size(cn);
    for (int gn = 0; gn < proto_sequence_value_size_tmp; gn++) {
      hand_calculator_work.candidates_work[cn].add_open_meld(
          get_ref_proto_sequence_cgn(cn, gn), open_meld_cand, open_meld_cand_copy,
          open_meld_num_begin, open_meld_num_end, open_meld_must, kan_cand, ts_maps[cn],
          hand_calculator_work.cal_proto_sequence_value_work, omp_get_thread_num());
    }
  }
}

void Hand_Calculator::set_cand_graph_sub_child(int cn1, int gn1, int cn2, int tile_in, int tile_out,
                                               Action_Type action_id, int tile0, int tile1,
                                               int tile2,
                                               boost::unordered_map<Hand_State2, int> *ts_maps,
                                               const int thread_num) {
  // This check prevents moves that the mahjong rules forbid.
  if (get_const_proto_sequence_cgn(cn1, gn1).get_riichi() == 1 && action_id != AT_CONCEALED_KAN) {
    return;
  } else if (action_id != AT_CONCEALED_KAN && action_id != AT_UPGRADED_KAN &&
             tile_kind(tile_out) == tile_kind(tile0)) {
    return;
  } else if (is_chii_low(action_id) && tile_kind(tile_out) == tile_kind(tile0) + 3) {
    return;
  } else if (is_chii_high(action_id) && tile_kind(tile_out) == tile_kind(tile0) - 3) {
    return;
  }

  // This check prevents an open meld that contains a red five and a discard of a red five. This
  // case must be rejected before this point.
  if (tile_out % 10 == 0 && tile_out > 0 && action_id != 0) {
    if (tile0 == tile_out || tile1 == tile_out || tile2 == tile_out) {
      assert(action_id == 2 || action_id == 11 || action_id == 12 || action_id == 13);
      return;
    }
  }

  Hand_State2 hand_state = get_const_proto_sequence_cgn(cn1, gn1).hand_state;
  boost::unordered_map<Hand_State2, int>::iterator itr;

  if (action_id != AT_OPEN_KAN) {
    if (tile_in % 10 == 0 && tile_in > 0) {
      hand_state.set_red_inside((tile_in - 1) / 10, 1);
    }
    if (tile_out % 10 == 0 && tile_out > 0) {
      hand_state.set_red_inside((tile_out - 1) / 10, 0);
    }
  }
  if (action_id == AT_UPGRADED_KAN) {
    hand_state.delete_pon(tile_kind(tile_out));
    hand_state.add_open_kan(tile_kind(tile_out));
    if (tile_out % 10 == 0 && tile_out > 0) {
      hand_state.set_red_inside((tile_out - 1) / 10, 0);
      hand_state.set_red_outside((tile_out - 1) / 10, 1);
    }
  } else if (action_id == AT_OPEN_KAN) {
    hand_state.add_open_kan(tile_kind(tile_in));
    if (tile_in % 10 == 0 || tile0 % 10 == 0 || tile1 % 10 == 0 || tile2 % 10 == 0) {
      hand_state.set_red_inside((tile_in - 1) / 10, 0);
      hand_state.set_red_outside((tile_in - 1) / 10, 1);
    }
  } else if (action_id != 0) {
    hand_state.add_one_open_meld(open_meld_action_type_to_open_meld_type((int)action_id),
                                 std::min({tile_kind(tile0), tile_kind(tile1), tile_kind(tile2)}));
    if (tile0 % 10 == 0 || tile1 % 10 == 0 || tile2 % 10 == 0) {
      hand_state.set_red_inside((tile0 - 1) / 10, 0);
      hand_state.set_red_outside((tile0 - 1) / 10, 1);
    }
  }
  if (action_id == AT_CONCEALED_KAN) {
    if (tile0 % 10 == 0) {
      hand_state.set_red_inside((tile0 - 1) / 10, 0);
      hand_state.set_red_outside((tile0 - 1) / 10, 1);
    }
  }
  itr = ts_maps[cn2].find(hand_state);
  if (itr != ts_maps[cn2].end()) {
    Hand_Action hand_action;
    hand_action.tile = tile_in;
    hand_action.tile_out = tile_out;
    hand_action.dst_group = cn2;
    hand_action.action_type = action_id;
    hand_action.dst_group_sub = itr->second;
    if (hand_action.action_type == AT_TSUMO || hand_action.action_type == AT_CONCEALED_KAN ||
        hand_action.action_type == AT_UPGRADED_KAN) {
      if (hand_calculator_work.cand_graph_sub_tsumo_work[thread_num].size() <
          MAX_EDGE_NUM_PER_THREAD) {
        hand_calculator_work.cand_graph_sub_tsumo_work[thread_num].push_back(hand_action);
      } else {
        // TODO: Report the tsumo-action capacity limit.
      }
    } else {
      if (hand_calculator_work.cand_graph_sub_open_meld_work[thread_num].size() <
          MAX_EDGE_NUM_PER_THREAD) {
        hand_calculator_work.cand_graph_sub_open_meld_work[thread_num].push_back(hand_action);
      } else {
        // TODO: Report the open-meld-action capacity limit.
      }
    }

    if ((hand_action.action_type == AT_TSUMO || hand_action.action_type == AT_CONCEALED_KAN) &&
        get_const_proto_sequence_cgn(hand_action.dst_group, hand_action.dst_group_sub)
                .get_tenpai() == 1 &&
        get_const_proto_sequence_cgn(hand_action.dst_group, hand_action.dst_group_sub)
                    .get_open_meld_num() -
                get_const_proto_sequence_cgn(hand_action.dst_group, hand_action.dst_group_sub)
                    .get_concealed_kan_num() ==
            0 &&
        get_const_proto_sequence_cgn(hand_action.dst_group, hand_action.dst_group_sub)
                .get_riichi() == 0) {
      hand_state.set_riichi(1);
      itr = ts_maps[hand_action.dst_group].find(hand_state);
      if (itr != ts_maps[hand_action.dst_group].end()) {
        if (hand_calculator_work.cand_graph_sub_tsumo_work[thread_num].size() <
            MAX_EDGE_NUM_PER_THREAD) {
          hand_action.dst_group_sub = itr->second;
          hand_calculator_work.cand_graph_sub_tsumo_work[thread_num].push_back(hand_action);
        } else {
          // TODO: Report the riichi-action capacity limit.
        }
      }
    }
  }
}

void Hand_Calculator::set_cand_graph_sub(const int cn1,
                                         const std::vector<std::array<int, 3>> &tsumo_edge_array,
                                         boost::unordered_map<Hand_State2, int> *ts_maps,
                                         const int thread_num) {
  for (int gn1 = 0; gn1 < hand_calculator_work.candidates_work[cn1].proto_sequence_loc.size();
       gn1++) {
    const Hand_Location &loc_tmp =
        hand_calculator_work.candidates_work[cn1].proto_sequence_loc[gn1];
    const int loc_tmp_first = loc_tmp.get_first();
    const int loc_tmp_second = loc_tmp.get_second();
    hand_calculator_work.cand_graph_sub_tsumo_loc[loc_tmp_first][loc_tmp_second][0] = thread_num;
    hand_calculator_work.cand_graph_sub_tsumo_loc[loc_tmp_first][loc_tmp_second][1] =
        hand_calculator_work.cand_graph_sub_tsumo_work[thread_num].size();
    hand_calculator_work.cand_graph_sub_open_meld_loc[loc_tmp_first][loc_tmp_second][0] =
        thread_num;
    hand_calculator_work.cand_graph_sub_open_meld_loc[loc_tmp_first][loc_tmp_second][1] =
        hand_calculator_work.cand_graph_sub_open_meld_work[thread_num].size();
    for (int i = 0; i < tsumo_edge_array.size(); i++) {
      const int tile = tsumo_edge_array[i][0];
      const int tile_out = tsumo_edge_array[i][1];
      const int cn2 = tsumo_edge_array[i][2];
      int tile_out_tmp = tile_out;
      int red_choice;
      if (tile_out < 30 && tile_out % 10 == 5) {
        if (get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile_out + 5) == 0) {
          red_choice = 1;
        } else if (get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile_out) > 0 &&
                   get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile_out + 5) > 0) {
          red_choice = 2;
        } else if (get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile_out + 5) > 0) {
          red_choice = 1;
          tile_out_tmp += 5;
        } else {
          continue;
        }
      } else {
        red_choice = 1;
      }
      for (int j = 0; j < red_choice; j++) {
        tile_out_tmp += 5 * j;

        if (get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile_out_tmp) == 0) {
          continue;
        }

        set_cand_graph_sub_child(cn1, gn1, cn2, tile, tile_out_tmp, AT_TSUMO, 0, 0, 0, ts_maps,
                                 thread_num);

        if (get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile) >= 2) {
          set_cand_graph_sub_child(cn1, gn1, cn2, tile, tile_out_tmp, AT_PON, tile, tile, tile,
                                   ts_maps, thread_num);
        }
        if (tile < 30 && tile % 10 == 5 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile) >= 1 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile + 5) == 1) {
          set_cand_graph_sub_child(cn1, gn1, cn2, tile, tile_out_tmp, AT_PON, tile, tile, tile + 5,
                                   ts_maps, thread_num);
        }
        if (tile % 10 == 5 && get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile) >= 2 &&
            open_meld_cand_tile == tile + 5) {
          set_cand_graph_sub_child(cn1, gn1, cn2, tile + 5, tile_out_tmp, AT_PON, tile + 5, tile,
                                   tile, ts_maps, thread_num);
        }

        if (tile < 30 && tile % 10 <= 7 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile + 1) > 0 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile + 2) > 0) {
          set_cand_graph_sub_child(cn1, gn1, cn2, tile, tile_out_tmp, AT_CHII_LOW, tile, tile + 1,
                                   tile + 2, ts_maps, thread_num);
        }
        if (tile < 30 && tile % 10 == 3 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile + 1) > 0 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile + 7) > 0) {
          set_cand_graph_sub_child(cn1, gn1, cn2, tile, tile_out_tmp, AT_CHII_LOW, tile, tile + 1,
                                   tile + 7, ts_maps, thread_num);
        }
        if (tile < 30 && tile % 10 == 4 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile + 6) > 0 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile + 2) > 0) {
          set_cand_graph_sub_child(cn1, gn1, cn2, tile, tile_out_tmp, AT_CHII_LOW, tile, tile + 6,
                                   tile + 2, ts_maps, thread_num);
        }
        if (tile % 10 == 5 && get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile + 1) > 0 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile + 2) > 0 &&
            open_meld_cand_tile == tile + 5) {
          set_cand_graph_sub_child(cn1, gn1, cn2, tile + 5, tile_out_tmp, AT_CHII_LOW, tile + 5,
                                   tile + 1, tile + 2, ts_maps, thread_num);
        }

        if (tile < 30 && 2 <= tile % 10 && tile % 10 <= 8 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile - 1) > 0 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile + 1) > 0) {
          set_cand_graph_sub_child(cn1, gn1, cn2, tile, tile_out_tmp, AT_CHII_MIDDLE, tile,
                                   tile - 1, tile + 1, ts_maps, thread_num);
        }
        if (tile < 30 && tile % 10 == 4 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile - 1) > 0 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile + 6) > 0) {
          set_cand_graph_sub_child(cn1, gn1, cn2, tile, tile_out_tmp, AT_CHII_MIDDLE, tile,
                                   tile - 1, tile + 6, ts_maps, thread_num);
        }
        if (tile < 30 && tile % 10 == 6 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile + 4) > 0 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile + 1) > 0) {
          set_cand_graph_sub_child(cn1, gn1, cn2, tile, tile_out_tmp, AT_CHII_MIDDLE, tile,
                                   tile + 4, tile + 1, ts_maps, thread_num);
        }
        if (tile % 10 == 5 && get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile - 1) > 0 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile + 1) > 0 &&
            open_meld_cand_tile == tile + 5) {
          set_cand_graph_sub_child(cn1, gn1, cn2, tile + 5, tile_out_tmp, AT_CHII_MIDDLE, tile + 5,
                                   tile - 1, tile + 1, ts_maps, thread_num);
        }

        if (tile < 30 && 3 <= tile % 10 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile - 2) > 0 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile - 1) > 0) {
          set_cand_graph_sub_child(cn1, gn1, cn2, tile, tile_out_tmp, AT_CHII_HIGH, tile, tile - 2,
                                   tile - 1, ts_maps, thread_num);
        }
        if (tile < 30 && tile % 10 == 6 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile - 2) > 0 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile + 4) > 0) {
          set_cand_graph_sub_child(cn1, gn1, cn2, tile, tile_out_tmp, AT_CHII_HIGH, tile, tile - 2,
                                   tile + 4, ts_maps, thread_num);
        }
        if (tile < 30 && tile % 10 == 7 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile + 3) > 0 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile - 1) > 0) {
          set_cand_graph_sub_child(cn1, gn1, cn2, tile, tile_out_tmp, AT_CHII_HIGH, tile, tile + 3,
                                   tile - 1, ts_maps, thread_num);
        }
        if (tile % 10 == 5 && get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile - 2) > 0 &&
            get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile - 1) > 0 &&
            open_meld_cand_tile == tile + 5) {
          set_cand_graph_sub_child(cn1, gn1, cn2, tile + 5, tile_out_tmp, AT_CHII_HIGH, tile + 5,
                                   tile - 2, tile - 1, ts_maps, thread_num);
        }

        if (get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile_out_tmp) == 4) {
          set_cand_graph_sub_child(cn1, gn1, cn2, tile, tile_out_tmp, AT_CONCEALED_KAN,
                                   tile_out_tmp, tile_out_tmp, tile_out_tmp, ts_maps, thread_num);
        } else if (tile_out_tmp < 30 && tile_out_tmp % 10 == 5 &&
                   get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile_out_tmp) == 3 &&
                   get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile_out_tmp + 5) == 1) {
          set_cand_graph_sub_child(cn1, gn1, cn2, tile, tile_out_tmp, AT_CONCEALED_KAN,
                                   tile_out_tmp, tile_out_tmp, tile_out_tmp + 5, ts_maps,
                                   thread_num);
        }

        if (get_const_proto_sequence_cgn(cn1, gn1).hand_state.get_pon_num(
                tile_kind(tile_out_tmp))) {
          set_cand_graph_sub_child(cn1, gn1, cn2, tile, tile_out_tmp, AT_UPGRADED_KAN, 0, 0, 0,
                                   ts_maps, thread_num);
        }
      }
    }

    for (int tile = 1; tile < 38; tile++) {
      if (tile % 10 != 0) {
        if (has_red_dora && tile < 30 && tile % 10 == 5) {
          if (get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile + 5) != 0 &&
              get_const_proto_sequence_cgn(cn1, gn1).using_tile_kind_num(tile) < 4 &&
              get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile) < 3)
            set_cand_graph_sub_child(cn1, gn1, cn1, tile, tile + 5, AT_TSUMO, 0, 0, 0, ts_maps,
                                     thread_num);
          if ((get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile) == 2 &&
               get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile + 5) == 1) ||
              get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile) == 3) {
            set_cand_graph_sub_child(cn1, gn1, cn1, tile, tile, AT_CONCEALED_KAN, tile, tile,
                                     tile + 5, ts_maps, thread_num);
          }
        } else {
          if (get_const_proto_sequence_cgn(cn1, gn1).count_tile(tile) == 3) {
            set_cand_graph_sub_child(cn1, gn1, cn1, tile, tile, AT_CONCEALED_KAN, tile, tile, tile,
                                     ts_maps, thread_num);
            set_cand_graph_sub_child(cn1, gn1, cn1, tile, 0, AT_OPEN_KAN, tile, tile, tile, ts_maps,
                                     thread_num);
          }
        }
        if (get_const_proto_sequence_cgn(cn1, gn1).hand_state.get_pon_num(tile) == 1) {
          set_cand_graph_sub_child(cn1, gn1, cn1, tile, tile, AT_UPGRADED_KAN, 0, 0, 0, ts_maps,
                                   thread_num);
        }
      }
    }
    hand_calculator_work.cand_graph_sub_tsumo_loc[loc_tmp_first][loc_tmp_second][2] =
        hand_calculator_work.cand_graph_sub_tsumo_work[thread_num].size();
    hand_calculator_work.cand_graph_sub_open_meld_loc[loc_tmp_first][loc_tmp_second][2] =
        hand_calculator_work.cand_graph_sub_open_meld_work[thread_num].size();
  }
}

void Hand_Calculator::set_candidates3_single_thread(const Game_State &game_state,
                                                    Hand_Analyzer hand_analyzer,
                                                    Hand_Analyzer hand_analyzer_af,
                                                    const int cn_max, const Tactics &tactics) {
  int hand_kind_counts[38];
  get_hand_kind_counts_proto_sequence(hand_analyzer, hand_kind_counts);
  bit_tile_num.set_from_array38(hand_kind_counts);

  Hand_Change hand_change_tmp;
  hand_change_tmp.hand_base = bit_tile_num;
  hand_change_tmp.id = 0;
  in0num = 0;

  clock_t check0 = clock();

  if (hand_analyzer.get_hand_num() + hand_analyzer.get_open_meld_num() * 3 == 14 &&
      open_meld_cand_tile == 0) {
    set_tsumo_node_1out(hand_analyzer);
  } else if (hand_analyzer.get_hand_num() + hand_analyzer.get_open_meld_num() * 3 == 13) {
    set_tsumo_node_1in_1out(game_state, hand_analyzer, hand_change_tmp);
  }

  if (!game_state.player_state[my_pid].riichi_declared) {
    if (hand_analyzer.get_shanten_num() != -1) {
      for (int shanten_num = 0; shanten_num <= cn_max; shanten_num++) {
        int meld_node = 0;
        int size_tmp = candidates_size();
        set_tsumo_node_meld(bit_tile_num, hand_all_num, hand_analyzer, open_meld_cand_tile,
                            shanten_num, hand_calculator_work.candidates_work, tcs, in0num);
        if (candidates_size() > size_tmp && meld_node == 0) {
          meld_node = 1;
        }
        if (shanten_num <= hand_analyzer.get_seven_pairs_shanten_num()) {
          set_tsumo_node_seven_pairs(hand_analyzer, shanten_num);
        }
        if (open_meld_cand_tile != 0) {
          set_tsumo_node_meld(bit_tile_num, hand_all_num, hand_analyzer_af, open_meld_cand_tile,
                              shanten_num, hand_calculator_work.candidates_work, tcs, in0num);
        }
        if (console_out) {
          std::cout << shanten_num << " " << cn_max << " " << candidates_size() << " " << meld_node
                    << std::endl;
        }
      }
    } else {
    }
  }

  // The ordinary candidate set is the bounded value domain. Close only its
  // nearest root-relative neighborhood over legal future calls. A candidate
  // group's provenance is not a legality rule: after a call of `tile_in` and
  // a discard of `tile_out`, the owner coordinate is C + tile_in - tile_out.
  // Keeping owners whose source distance is at most one and whose destination
  // remains within cn_max repairs missing call continuations without expanding
  // the complete legal Mahjong state space.
  if (open_meld_cand_tile != 0 && !game_state.player_state[my_pid].riichi_declared &&
      tactics.max_open_meld_num > 0) {
    const int candidate_seed_size = candidates_size();
    const auto incoming_distance = [&](const Bit_Tile_Num &hand) {
      int result = 0;
      for (int tile = 1; tile < 38; ++tile) {
        if (tile % 10 == 0) continue;
        result += std::max(hand.count_tile(tile) - bit_tile_num.count_tile(tile), 0);
      }
      return result;
    };
    const auto add_owner = [&](const Hand_Change &source, const int tile_in, const int tile_out) {
      Hand_Change owner = source;
      owner.hand_base.add_tile(tile_in);
      owner.hand_base.delete_tile(tile_out);
      if (incoming_distance(owner.hand_base) <= cn_max)
        set_tsumo_node_exec(owner, false, hand_calculator_work.candidates_work, tcs, in0num);
    };
    for (int cn = 0; cn < candidate_seed_size; ++cn) {
      const Hand_Change source = hand_calculator_work.candidates_work[cn].hand_change;
      if (incoming_distance(source.hand_base) > 1) continue;
      for (int tile_in = 1; tile_in < 38; ++tile_in) {
        if (tile_in % 10 == 0 || source.hand_base.count_tile(tile_in) >= 4) continue;

        if (source.hand_base.count_tile(tile_in) >= 2) {
          for (int tile_out = 1; tile_out < 38; ++tile_out) {
            if (tile_out % 10 == 0 || tile_out == tile_in) continue;
            const int remaining =
                source.hand_base.count_tile(tile_out) - (tile_out == tile_in ? 2 : 0);
            if (remaining > 0) add_owner(source, tile_in, tile_out);
          }
        }

        if (tile_in >= 30) continue;
        const int suit_begin = tile_in / 10 * 10 + 1;
        for (int base = std::max(suit_begin, tile_in - 2);
             base <= std::min(suit_begin + 6, tile_in); ++base) {
          int consumed0 = 0;
          int consumed1 = 0;
          for (int tile = base; tile <= base + 2; ++tile) {
            if (tile == tile_in) continue;
            if (consumed0 == 0)
              consumed0 = tile;
            else
              consumed1 = tile;
          }
          if (source.hand_base.count_tile(consumed0) == 0 ||
              source.hand_base.count_tile(consumed1) == 0)
            continue;
          for (int tile_out = 1; tile_out < 38; ++tile_out) {
            if (tile_out % 10 == 0 || tile_out == tile_in) continue;
            if (tile_in == base && tile_out == tile_in + 3) continue;
            if (tile_in == base + 2 && tile_out == tile_in - 3) continue;
            int remaining = source.hand_base.count_tile(tile_out);
            remaining -= tile_out == consumed0;
            remaining -= tile_out == consumed1;
            if (remaining > 0) add_owner(source, tile_in, tile_out);
          }
        }
      }
    }
  }

  clock_t check1 = clock();

  if (console_out) {
    std::cout << "time graph_node:" << (double)(check1 - check0) / CLOCKS_PER_SEC << std::endl;
  }

  set_in_out(hand_analyzer);
  if (open_meld_cand_tile != 0) {
    set_in_out(hand_analyzer_af);
  }
}

void Hand_Calculator::set_candidates3_multi_thread(const Moves &game_record,
                                                   const Game_State &game_state,
                                                   const std::array<bool, 38> discard,
                                                   Hand_Analyzer hand_analyzer,
                                                   const Tactics &tactics) {
  boost::unordered_map<Hand_State2, int> *ts_maps;
  ts_maps = new boost::unordered_map<Hand_State2, int>[candidates_size()];

  clock_t check1 = clock();

#pragma omp parallel
#pragma omp for
  for (int cn = 0; cn < candidates_size(); cn++) {
    hand_calculator_work.candidates_work[cn].set_proto_sequence_value_init(
        bit_tile_num, hand_analyzer.hand_state, open_meld_cand_tile, ts_maps[cn],
        hand_calculator_work.cal_proto_sequence_value_work, omp_get_thread_num());
  }

  clock_t check2 = clock();
  const bool incoming_red = open_meld_cand_tile != 0 && open_meld_cand_tile % 10 == 0;
  if (!game_state.player_state[my_pid].riichi_declared &&
      (hand_analyzer.hand_state.get_red_inside(0) || hand_analyzer.hand_state.get_red_inside(1) ||
       hand_analyzer.hand_state.get_red_inside(2) || incoming_red)) {
    for (int worker = 0; worker < CAL_NUM_THREAD; ++worker)
      hand_calculator_work.tsumo_edge_work[worker].clear();
#pragma omp parallel for
    for (int cn = 0; cn < candidates_size(); ++cn)
      set_tsumo_edge(hand_calculator_work.candidates_work[cn].hand_change, omp_get_thread_num());
    std::vector<std::vector<std::array<int, 3>>> exchange(candidates_size());
    for (int worker = 0; worker < CAL_NUM_THREAD; ++worker) {
      for (const auto &edge : hand_calculator_work.tsumo_edge_work[worker]) {
        exchange[edge.src_group].push_back({edge.tile_in, edge.tile_out, edge.dst_group});
        exchange[edge.dst_group].push_back({edge.tile_out, edge.tile_in, edge.src_group});
      }
    }
    std::vector<std::array<int, 2>> pending;
    for (int cn = 0; cn < candidates_size(); ++cn)
      for (int tile = 5; tile < 30; tile += 10) exchange[cn].push_back({tile, tile, cn});
    for (int cn = 0; cn < candidates_size(); ++cn)
      for (int gn = 0; gn < group_size(cn); ++gn) pending.push_back({cn, gn});
    // Preserve physical red identity along the frozen owner graph.
    for (std::size_t next = 0; next < pending.size(); ++next) {
      const int cn = pending[next][0];
      const Hand_Analyzer_Basic source = get_const_proto_sequence_cgn(cn, pending[next][1]);
      for (const auto &edge : exchange[cn]) {
        const int tile_in = edge[0];
        const int kind_out = edge[1];
        const int dst = edge[2];
        if (tile_in < 30 && tile_in % 10 == 5 && source.count_tile(tile_in) >= 3) continue;
        for (int red_out = 0; red_out < 2; ++red_out) {
          if (red_out && !(kind_out < 30 && kind_out % 10 == 5)) continue;
          const int tile_out = kind_out + 5 * red_out;
          if (source.count_tile(tile_out) == 0) continue;
          auto destination = source;
          destination.add_tile(tile_in);
          destination.delete_tile(tile_out);
          if (ts_maps[dst].find(destination.hand_state) != ts_maps[dst].end()) continue;
          const int worker = dst % CAL_NUM_THREAD;
          auto &storage = hand_calculator_work.cal_proto_sequence_value_work[worker];
          auto &locations = hand_calculator_work.candidates_work[dst].proto_sequence_loc;
          const int gn = locations.size();
          locations.push_back(Hand_Location(worker, storage.size()));
          storage.push_back(destination);
          ts_maps[dst][destination.hand_state] = gn;
          pending.push_back({dst, gn});
        }
      }
    }
  }
  add_open_meld_node(game_state, 0, candidates_size(), 1, tactics.max_open_meld_num, 0, 0, ts_maps);
  if (!game_state.player_state[my_pid].riichi_declared &&
      (hand_analyzer.hand_state.get_red_inside(0) || hand_analyzer.hand_state.get_red_inside(1) ||
       hand_analyzer.hand_state.get_red_inside(2) || incoming_red)) {
    std::vector<std::vector<std::array<int, 3>>> exchange(candidates_size());
    for (int worker = 0; worker < CAL_NUM_THREAD; ++worker)
      for (const auto &edge : hand_calculator_work.tsumo_edge_work[worker]) {
        exchange[edge.src_group].push_back({edge.tile_in, edge.tile_out, edge.dst_group});
        exchange[edge.dst_group].push_back({edge.tile_out, edge.tile_in, edge.src_group});
      }
    std::vector<std::array<int, 2>> pending;
    for (int cn = 0; cn < candidates_size(); ++cn)
      for (int gn = 0; gn < group_size(cn); ++gn) {
        const auto &state = get_const_proto_sequence_cgn(cn, gn).hand_state;
        if (state.get_red_outside(0) || state.get_red_outside(1) || state.get_red_outside(2))
          pending.push_back({cn, gn});
      }
    for (std::size_t next = 0; next < pending.size(); ++next) {
      const int cn = pending[next][0];
      const auto source = get_const_proto_sequence_cgn(cn, pending[next][1]);
      for (const auto &edge : exchange[cn]) {
        const int tile_in = edge[0];
        const int tile_out = edge[1];
        const int dst = edge[2];
        if (source.count_tile(tile_out) == 0 || source.using_tile_kind_num(tile_in) >= 4) continue;
        auto destination = source;
        destination.add_tile(tile_in);
        destination.delete_tile(tile_out);
        if (ts_maps[dst].find(destination.hand_state) != ts_maps[dst].end()) continue;
        auto ordinary = destination.hand_state;
        for (int color = 0; color < 3; ++color) {
          if (ordinary.get_red_outside(color) &&
              destination.using_tile_kind_num(color * 10 + 5) == 4 &&
              destination.count_tile_kind(color * 10 + 5) > 0)
            ordinary.set_red_inside(color, 1);
          ordinary.set_red_outside(color, 0);
        }
        if (ts_maps[dst].find(ordinary) == ts_maps[dst].end()) continue;
        const int worker = dst % CAL_NUM_THREAD;
        auto &storage = hand_calculator_work.cal_proto_sequence_value_work[worker];
        auto &locations = hand_calculator_work.candidates_work[dst].proto_sequence_loc;
        const int gn = locations.size();
        locations.push_back(Hand_Location(worker, storage.size()));
        storage.push_back(destination);
        ts_maps[dst][destination.hand_state] = gn;
        pending.push_back({dst, gn});
      }
    }
  }

  if (tactics.consider_kan) {
    // A concealed kan can exceed max_open_meld_num after this call.
    int concealed_kan_cand[38] = {};
    for (int tile = 1; tile < 38; tile++) {
      if (tile % 10 != 0) {
        if (game_state.player_state[my_pid].hand[tile] == 4 ||
            (tile < 30 && tile % 10 == 5 && game_state.player_state[my_pid].hand[tile] == 3 &&
             game_state.player_state[my_pid].hand[tile + 5] == 1)) {
          concealed_kan_cand[tile] = 1;
        }
      }
    }
#pragma omp parallel
#pragma omp for
    for (int cn = 0; cn < candidates_size(); cn++) {
      hand_calculator_work.candidates_work[cn].add_concealed_kan(
          concealed_kan_cand, ts_maps[cn], hand_calculator_work.cal_proto_sequence_value_work,
          omp_get_thread_num());
    }

    int upgraded_kan_cand[38] = {};
    for (int fn = 0; fn < game_state.player_state[my_pid].open_meld.size(); fn++) {
      if (game_state.player_state[my_pid].open_meld[fn].type == FT_PON) {
        int pon_tile = tile_kind(game_state.player_state[my_pid].open_meld[fn].tile);
        if (game_state.player_state[my_pid].hand[pon_tile] == 1 ||
            pon_tile < 30 && pon_tile % 10 == 5 &&
                game_state.player_state[my_pid].hand[pon_tile + 5] == 1) {
          upgraded_kan_cand[pon_tile] = 1;
        }
      }
    }
#pragma omp parallel
#pragma omp for
    for (int cn = 0; cn < candidates_size(); cn++) {
      hand_calculator_work.candidates_work[cn].add_upgraded_kan(
          upgraded_kan_cand, ts_maps[cn], hand_calculator_work.cal_proto_sequence_value_work,
          omp_get_thread_num());
    }

    int open_kan_cand[38] = {};
    if (open_meld_cand_tile != 0) {
      open_kan_cand[tile_kind(open_meld_cand_tile)] = 1;
    }
#pragma omp parallel
#pragma omp for
    for (int cn = 0; cn < candidates_size(); cn++) {
      hand_calculator_work.candidates_work[cn].add_open_kan(
          open_kan_cand, ts_maps[cn], hand_calculator_work.cal_proto_sequence_value_work,
          omp_get_thread_num());
    }
  }

#pragma omp parallel
#pragma omp for
  for (int cn = 0; cn < candidates_size(); cn++) {
    hand_calculator_work.candidates_work[cn].analyze_all_tenpai(
        my_pid, game_state, hand_calculator_work.cal_proto_sequence_value_work);
  }

  if (!game_state.player_state[my_pid].riichi_declared) {
#pragma omp parallel
#pragma omp for
    for (int cn = 0; cn < candidates_size(); cn++) {
      hand_calculator_work.candidates_work[cn].add_riichi(
          ts_maps[cn], hand_calculator_work.cal_proto_sequence_value_work, omp_get_thread_num());
    }
  }

  for (int i = 0; i < CAL_NUM_THREAD; i++) {
    hand_calculator_work.win_graph_work[i].clear();
  }

#pragma omp parallel
#pragma omp for
  for (int cn = 0; cn < candidates_size(); cn++) {
    hand_calculator_work.candidates_work[cn].analyze_all_win(
        my_pid, game_state, hand_calculator_work.cal_proto_sequence_value_work,
        hand_calculator_work.win_graph_work[omp_get_thread_num()],
        hand_calculator_work.win_graph_loc, omp_get_thread_num());
  }

  if (console_out) {
    std::cout << "sub_num_all:" << get_sub_num_all() << std::endl;
  }

  for (int cn = 0; cn < candidates_size(); cn++) {
    std::vector<int> furiten_cand;
    for (int tile = 1; tile < 38; tile++) {
      const bool removed_draw = game_state.player_state[my_pid].riichi_declared &&
                                game_record.back().type == EventType::DRAW &&
                                game_record.back().player == my_pid &&
                                tile_kind(game_record.back().tile) == tile;
      if (tile % 10 != 0 && hand_analyzer.using_tile_kind_num(tile) + removed_draw >
                                get_const_proto_sequence_cgn(cn, 0).using_tile_kind_num(tile)) {
        furiten_cand.push_back(tile);
      }
    }
    for (int gn = 0; gn < group_size(cn); gn++) {
      const std::array<int, 3> &win_loc = hand_calculator_work.get_const_win_loc(cn, gn);
      for (int an = win_loc[1]; an < win_loc[2]; an++) {
        const Win_Calc &win = hand_calculator_work.win_graph_work[win_loc[0]][an];
        if (discard[win.win_info.get_tile()] == 1) {
          get_ref_proto_sequence_cgn(cn, gn).set_furiten(1);
        }
        for (int i = 0; i < furiten_cand.size(); i++) {
          if (furiten_cand[i] == win.win_info.get_tile()) {
            get_ref_proto_sequence_cgn(cn, gn).set_furiten(1);
          }
        }
      }
    }
  }

  clock_t check3 = clock();

  for (int i = 0; i < CAL_NUM_THREAD; i++) {
    hand_calculator_work.tsumo_edge_work[i].clear();
  }

#pragma omp parallel
#pragma omp for
  for (int cn = 0; cn < candidates_size(); cn++) {
    set_tsumo_edge(hand_calculator_work.candidates_work[cn].hand_change, omp_get_thread_num());
  }

  clock_t check4 = clock();

  for (int i = 0; i < CAL_NUM_THREAD; i++) {
    hand_calculator_work.cand_graph_sub_tsumo_work[i].clear();
    hand_calculator_work.cand_graph_sub_open_meld_work[i].clear();
  }

  std::array<std::array<int, 38>, 38> *graph_edge_new;
  // A static allocation of MAX_CANDIDATES_NUM stops the program. We doubt that the allocation works
  // when candidates_size is large.
  graph_edge_new = new std::array<std::array<int, 38>, 38>[candidates_size()];
#pragma omp parallel
#pragma omp for
  for (int cn = 0; cn < candidates_size(); cn++) {
    for (int tile_in = 0; tile_in < 38; tile_in++) {
      for (int tile_out = 0; tile_out < 38; tile_out++) {
        graph_edge_new[cn][tile_in][tile_out] = -1;
      }
    }
  }

#pragma omp parallel
#pragma omp for
  for (int i = 0; i < CAL_NUM_THREAD; i++) {
    for (int j = 0; j < hand_calculator_work.tsumo_edge_work[i].size(); j++) {
      const Tsumo_Edge &tsumo_edge = hand_calculator_work.tsumo_edge_work[i][j];
      graph_edge_new[tsumo_edge.src_group][tsumo_edge.tile_in][tsumo_edge.tile_out] =
          tsumo_edge.dst_group;
      graph_edge_new[tsumo_edge.dst_group][tsumo_edge.tile_out][tsumo_edge.tile_in] =
          tsumo_edge.src_group;
    }
  }

  int tsumo_edge_num_all = 0;
  for (int cn = 0; cn < candidates_size(); cn++) {
    for (int tile = 0; tile < 38; tile++) {
      for (int tile_out = 0; tile_out < 38; tile_out++) {
        if (graph_edge_new[cn][tile][tile_out] != -1) {
          tsumo_edge_num_all++;
        }
      }
    }
  }
  if (console_out) {
    std::cout << "tsumo_edge_num_all:" << tsumo_edge_num_all << std::endl;
  }

#pragma omp parallel
#pragma omp for
  for (int cn1 = 0; cn1 < candidates_size(); cn1++) {
    std::vector<std::array<int, 3>> tsumo_edge_array;
    for (int tile = 1; tile < 38; tile++) {
      for (int tile_out = 0; tile_out < 38; tile_out++) {
        if (graph_edge_new[cn1][tile][tile_out] != -1) {
          tsumo_edge_array.push_back(
              std::array<int, 3>({tile, tile_out, graph_edge_new[cn1][tile][tile_out]}));
        }
      }
    }
    set_cand_graph_sub(cn1, tsumo_edge_array, ts_maps, omp_get_thread_num());
  }

  int action_num_all_work[2] = {};
  for (int i = 0; i < CAL_NUM_THREAD; i++) {
    action_num_all_work[0] += hand_calculator_work.cand_graph_sub_tsumo_work[i].size();
    action_num_all_work[1] += hand_calculator_work.cand_graph_sub_open_meld_work[i].size();
  }

  if (console_out) {
    std::cout << "action_num_all:" << action_num_all_work[0] << " " << action_num_all_work[1]
              << std::endl;
  }

  for (int cn = 0; cn < candidates_size(); cn++) {
    ts_maps[cn].clear();
  }
  delete[] ts_maps;
  delete[] graph_edge_new;

  clock_t check5 = clock();
  if (console_out) {
    std::cout << "time";
    std::cout << " no_open_meld_analysis:" << (double)(check2 - check1) / CLOCKS_PER_SEC;
    std::cout << " add_open_meld:" << (double)(check3 - check2) / CLOCKS_PER_SEC << std::endl;
    std::cout << "graph_edge:" << (double)(check4 - check3) / CLOCKS_PER_SEC;
    std::cout << " graph_edge_sub:" << (double)(check5 - check4) / CLOCKS_PER_SEC << std::endl;
  }
}

int Hand_Calculator::get_max_group_size() {
  int result = 0;
  for (int cn = 0; cn < candidates_size(); cn++) {
    if (result < group_size(cn)) {
      result = group_size(cn);
    }
  }
  return result;
}

int Hand_Calculator::get_max_edge_size() {
  int result = 0;
  for (int cn = 0; cn < candidates_size(); cn++) {
    for (int gn = 0; gn < group_size(cn); gn++) {
      const Hand_Location &proto_sequence_loc =
          hand_calculator_work.candidates_work[cn].proto_sequence_loc[gn];
      const int proto_sequence_loc_first = proto_sequence_loc.get_first();
      const int proto_sequence_loc_second = proto_sequence_loc.get_second();
      const std::array<int, 3> &edge_tsumo_loc =
          hand_calculator_work
              .cand_graph_sub_tsumo_loc[proto_sequence_loc_first][proto_sequence_loc_second];
      if (result < edge_tsumo_loc[2] - edge_tsumo_loc[1]) {
        result = edge_tsumo_loc[2] - edge_tsumo_loc[1];
      }
      const std::array<int, 3> &edge_open_meld_loc =
          hand_calculator_work
              .cand_graph_sub_open_meld_loc[proto_sequence_loc_first][proto_sequence_loc_second];
      if (result < edge_open_meld_loc[2] - edge_open_meld_loc[1]) {
        result = edge_open_meld_loc[2] - edge_open_meld_loc[1];
      }
    }
  }
  return result;
}

int Hand_Calculator::get_sub_num_all() {
  int result = 0;
  for (int cn = 0; cn < candidates_size(); cn++) {
    result += group_size(cn);
  }

  int result2 = 0;
  for (int i = 0; i < CAL_NUM_THREAD; i++) {
    result2 += hand_calculator_work.cal_proto_sequence_value_work[i].size();
  }
  assert(result == result2);
  return result;
}

void Hand_Calculator::set_win_shanten_num(const Game_State &game_state) {
  for (int cn = 0; cn < candidates_size(); cn++) {
    for (int gn = 0; gn < group_size(cn); gn++) {
      const std::array<int, 3> &win_loc = hand_calculator_work.get_const_win_loc(cn, gn);
      for (int an = win_loc[1]; an < win_loc[2]; an++) {
        const Win_Calc &win = hand_calculator_work.win_graph_work[win_loc[0]][an];
        if (win.get_points_tsumo(my_pid, game_state) > 0) {
          get_ref_proto_sequence_cgn(cn, gn).set_win_shanten_num(0);
          break;
        }
      }
    }
  }
  for (int shanten_num = 1; shanten_num <= 4; shanten_num++) {
    for (int cn = 0; cn < candidates_size(); cn++) {
      for (int gn = 0; gn < group_size(cn); gn++) {
        if (get_ref_proto_sequence_cgn(cn, gn).get_win_shanten_num() > shanten_num) {
          const std::array<int, 3> &tsumo_edge_loc =
              hand_calculator_work.get_const_tsumo_edge_loc(cn, gn);
          for (int acn = tsumo_edge_loc[1]; acn < tsumo_edge_loc[2]; acn++) {
            const Hand_Action &tsumo_edge =
                hand_calculator_work.cand_graph_sub_tsumo_work[tsumo_edge_loc[0]][acn];
            if (tsumo_edge.action_type == AT_TSUMO) {
              if (get_ref_proto_sequence_cgn(tsumo_edge.dst_group, tsumo_edge.dst_group_sub)
                      .get_win_shanten_num() == shanten_num - 1) {
                get_ref_proto_sequence_cgn(cn, gn).set_win_shanten_num(shanten_num);
                break;
              }
            }
          }
        }
      }
    }
  }
}

int Hand_Calculator::get_open_meld_win_shanten_num(const Game_State &game_state,
                                                   const Tile_Array &current_hand,
                                                   const bool chii_action) {
  int open_meld_win_shanten_num = 8;
  if (open_meld_cand_tile != 0) {
    set_win_shanten_num(game_state);
    int cn_open_meld_neg = 0;
    int gn_open_meld_neg = 0;
    for (int gn = 0; gn < group_size(cn_open_meld_neg); gn++) {
      if (is_same_hand_proto_sequence(current_hand,
                                      get_const_proto_sequence_cgn(cn_open_meld_neg, gn))) {
        gn_open_meld_neg = gn;
        break;
      }
    }
    if (console_out) {
      std::cout
          << "win_shanten_num:"
          << get_const_proto_sequence_cgn(cn_open_meld_neg, gn_open_meld_neg).get_win_shanten_num()
          << std::endl;
    }

    const std::array<int, 3> &open_meld_edge_loc =
        hand_calculator_work.get_const_open_meld_edge_loc(cn_open_meld_neg, gn_open_meld_neg);
    for (int acn = open_meld_edge_loc[1]; acn < open_meld_edge_loc[2]; acn++) {
      const Hand_Action &ac_tmp =
          hand_calculator_work.cand_graph_sub_open_meld_work[open_meld_edge_loc[0]][acn];
      if (ac_tmp.tile != open_meld_cand_tile) {
        continue;
      } else if (!chii_action && is_chii(ac_tmp.action_type)) {
        continue;
      } else {
        if (get_const_proto_sequence_cgn(ac_tmp.dst_group, ac_tmp.dst_group_sub)
                .get_win_shanten_num() < open_meld_win_shanten_num) {
          open_meld_win_shanten_num =
              get_const_proto_sequence_cgn(ac_tmp.dst_group, ac_tmp.dst_group_sub)
                  .get_win_shanten_num();
        }
      }
    }
  }
  if (console_out) {
    std::cout << "open_meld_win_shanten_num:" << open_meld_win_shanten_num << std::endl;
  }
  return open_meld_win_shanten_num;
}

void Hand_Calculator::set_win_exp(
    const Game_State &game_state,
    const std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> &round_end_pt_exp) {
  const Tile_Array tile_visible_kind = tile_kind(
      get_tile_visible_me(my_pid, game_state));  // The reference implementation does it this way.
#pragma omp parallel
#pragma omp for
  for (int cn = 0; cn < candidates_size(); cn++) {
    for (int gn = 0; gn < group_size(cn); gn++) {
      const std::array<int, 3> &win_loc = hand_calculator_work.get_const_win_loc(cn, gn);
      for (int an = win_loc[1]; an < win_loc[2]; an++) {
        Win_Calc &win = hand_calculator_work.win_graph_work[win_loc[0]][an];
        const std::array<double, 4> points_exp =
            win.get_points_exp(my_pid, get_const_proto_sequence_cgn(cn, gn).hand_bit,
                               get_const_proto_sequence_cgn(cn, gn).hand_state, tile_visible_kind,
                               game_state, round_end_pt_exp);
        win.tsumo_exp = points_exp[0];
        win.ron_exp = points_exp[1];
        // The backward analysis does not consider the red dora. If we consider the red dora, we
        // must modify this part.
      }
    }
  }
}

void Hand_Calculator::calc_win_prob(
    const int tsumo_num, const double exp_min, const Game_State &game_state,
    const std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> &round_end_pt_exp) {
  clock_t check0 = clock();
  const int remaining_tile_num = get_remaining_tile_num(my_pid, game_state);
  const Tile_Array tile_visible = tile_kind(get_tile_visible_wo_hand(my_pid, game_state));
  const Tile_Array using_tile_kind_array = tile_kind(using_tile_array(
      game_state.player_state[my_pid].hand, game_state.player_state[my_pid].open_meld));
  set_win_exp(game_state, round_end_pt_exp);
  clock_t check1 = clock();

#pragma omp parallel
#pragma omp for
  for (int cn = 0; cn < candidates_size(); cn++) {
    for (int gn = 0; gn < group_size(cn); gn++) {
      const Hand_Location &loc = hand_calculator_work.candidates_work[cn].proto_sequence_loc[gn];
      const int loc_first = loc.get_first();
      const int loc_second = loc.get_second();
      hand_calculator_work.win_prob_work[loc_first][loc_second][0] = 0.0;
      hand_calculator_work.win_prob_to_work[loc_first][loc_second][0] = 0.0;
      hand_calculator_work.tenpai_prob_work[loc_first][loc_second][0] =
          1.0 * get_const_proto_sequence_cgn(cn, gn).get_tenpai();
      hand_calculator_work.tenpai_prob_to_work[loc_first][loc_second][0] =
          1.0 * get_const_proto_sequence_cgn(cn, gn).get_tenpai();
      if (get_const_proto_sequence_cgn(cn, gn).get_riichi() == 1) {
        hand_calculator_work.points_exp_work[loc_first][loc_second][0] = 0.0;
        hand_calculator_work.points_exp_to_work[loc_first][loc_second][0] = 0.0;
      } else {
        hand_calculator_work.points_exp_work[loc_first][loc_second][0] = 0.0;
        hand_calculator_work.points_exp_to_work[loc_first][loc_second][0] = 0.0;
      }

      for (int han = 0; han < 5; han++) {
        hand_calculator_work.win_han_prob_work[loc_first][loc_second][0][han] = 0.0;
        hand_calculator_work.win_han_prob_to_work[loc_first][loc_second][0][han] = 0.0;
      }
    }
  }
  for (int tn = 1; tn <= tsumo_num; tn++) {
    const int mod2tn = mod2(tn);
    const int mod2tn_prev = mod2(tn - 1);
#pragma omp parallel
#pragma omp for
    for (int cn = 0; cn < candidates_size(); cn++) {
      for (int gn = 0; gn < group_size(cn); gn++) {
        const Hand_Location &loc = hand_calculator_work.candidates_work[cn].proto_sequence_loc[gn];
        const int loc_first = loc.get_first();
        const int loc_second = loc.get_second();
        hand_calculator_work.win_prob_to_work[loc_first][loc_second][mod2tn] =
            hand_calculator_work.win_prob_work[loc_first][loc_second][mod2tn_prev];
        hand_calculator_work.tenpai_prob_to_work[loc_first][loc_second][mod2tn] =
            hand_calculator_work.tenpai_prob_work[loc_first][loc_second][mod2tn_prev];
        hand_calculator_work.points_exp_to_work[loc_first][loc_second][mod2tn] =
            hand_calculator_work.points_exp_work[loc_first][loc_second][mod2tn_prev];
        for (int han = 0; han < 5; han++) {
          hand_calculator_work.win_han_prob_to_work[loc_first][loc_second][mod2tn][han] =
              hand_calculator_work.win_han_prob_work[loc_first][loc_second][mod2tn_prev][han];
        }

        double tenpai_tmpbest[38], exp_tmpbest[38];
        int tile_effective[38];
        const simple_dp_sparsemax::PointsRecord points_baseline = {
            hand_calculator_work.win_prob_work[loc_first][loc_second][mod2tn_prev],
            hand_calculator_work.points_exp_work[loc_first][loc_second][mod2tn_prev],
            hand_calculator_work.win_han_prob_work[loc_first][loc_second][mod2tn_prev]};
        simple_dp_sparsemax::TileCandidates points_candidates =
            simple_dp_sparsemax::baseline_per_tile(points_baseline);
        for (int tile = 0; tile < 38; tile++) {
          tile_effective[tile] = 0;
          tenpai_tmpbest[tile] =
              hand_calculator_work.tenpai_prob_work[loc_first][loc_second][mod2tn_prev];
          exp_tmpbest[tile] =
              hand_calculator_work.points_exp_work[loc_first][loc_second][mod2tn_prev];
        }

        const std::array<int, 3> &win_loc = hand_calculator_work.get_const_win_loc(cn, gn);
        for (int an = win_loc[1]; an < win_loc[2]; an++) {
          const Win_Calc &win = hand_calculator_work.win_graph_work[win_loc[0]][an];
          if (win.get_points_tsumo(my_pid, game_state) > 0) {
            std::array<double, 5> han_distribution = {{0, 0, 0, 0, 0}};
            han_distribution[std::min(4, win.win_info.get_han_tsumo())] = 1.0;
            points_candidates[win.win_info.get_tile()].push_back(
                simple_dp_sparsemax::Candidate({1.0, (win.tsumo_exp - exp_min), han_distribution}));
            tile_effective[win.win_info.get_tile()] = 1;

            if ((win.tsumo_exp - exp_min) > exp_tmpbest[win.win_info.get_tile()]) {
              tile_effective[win.win_info.get_tile()] = 1;
              exp_tmpbest[win.win_info.get_tile()] = (win.tsumo_exp - exp_min);
              // TODO: Include underneath dora in this probability.
            }
          }
        }

        const std::array<int, 3> &tsumo_edge_loc =
            hand_calculator_work.get_const_tsumo_edge_loc(cn, gn);
        for (int acn = tsumo_edge_loc[1]; acn < tsumo_edge_loc[2]; acn++) {
          const Hand_Action &tsumo_edge =
              hand_calculator_work.cand_graph_sub_tsumo_work[tsumo_edge_loc[0]][acn];
          const int dst_group = tsumo_edge.dst_group;
          const int dst_group_sub = tsumo_edge.dst_group_sub;
          const int tile = tsumo_edge.tile;
          const Hand_Location &dst_loc =
              hand_calculator_work.candidates_work[dst_group].proto_sequence_loc[dst_group_sub];
          const int dst_loc_first = dst_loc.get_first();
          const int dst_loc_second = dst_loc.get_second();
          if (hand_calculator_work.tenpai_prob_work[dst_loc_first][dst_loc_second][mod2tn_prev] >
              tenpai_tmpbest[tile]) {
            tile_effective[tile] = 1;
            tenpai_tmpbest[tile] =
                hand_calculator_work.tenpai_prob_work[dst_loc_first][dst_loc_second][mod2tn_prev];
          }
          if (hand_calculator_work.points_exp_work[dst_loc_first][dst_loc_second][mod2tn_prev] >
              exp_tmpbest[tile]) {
            if (get_const_proto_sequence_cgn(dst_group, dst_group_sub).get_negative() == 0) {
              tile_effective[tile] = 1;
              exp_tmpbest[tile] =
                  hand_calculator_work.points_exp_work[dst_loc_first][dst_loc_second][mod2tn_prev];
            }
          }
          if (get_const_proto_sequence_cgn(dst_group, dst_group_sub).get_negative() == 0) {
            points_candidates[tile].push_back(simple_dp_sparsemax::Candidate(
                {hand_calculator_work.win_prob_work[dst_loc_first][dst_loc_second][mod2tn_prev],
                 hand_calculator_work.points_exp_work[dst_loc_first][dst_loc_second][mod2tn_prev],
                 hand_calculator_work
                     .win_han_prob_work[dst_loc_first][dst_loc_second][mod2tn_prev]}));
            tile_effective[tile] = 1;
          }
        }
        for (int tile = 0; tile < 38; tile++) {
          if (tile % 10 != 0 && tile_effective[tile] == 1) {
            double remaining_tmp = 4.0 -
                                   get_const_proto_sequence_cgn(cn, gn).using_tile_kind_num(tile) -
                                   tile_visible[tile];
            if (using_tile_kind_array[tile] -
                    get_const_proto_sequence_cgn(cn, gn).using_tile_kind_num(tile) >
                0) {
              remaining_tmp -= using_tile_kind_array[tile] -
                               get_const_proto_sequence_cgn(cn, gn).using_tile_kind_num(tile);
            }
            if (tn != tsumo_num) {
              remaining_tmp *= rn_para;
            }
            if (tenpai_tmpbest[tile] >
                hand_calculator_work.tenpai_prob_work[loc_first][loc_second][mod2tn_prev]) {
              hand_calculator_work.tenpai_prob_to_work[loc_first][loc_second][mod2tn] +=
                  (tenpai_tmpbest[tile] -
                   hand_calculator_work.tenpai_prob_work[loc_first][loc_second][mod2tn_prev]) *
                  remaining_tmp / (remaining_tile_num - (tsumo_num - tn));
            }

            for (std::size_t i = 1; i < points_candidates[tile].size(); ++i)
              points_candidates[tile][i].event_weight =
                  remaining_tmp / (remaining_tile_num - (tsumo_num - tn));
            const simple_dp_sparsemax::PointsRecord delta =
                simple_dp_sparsemax::mix_delta(points_candidates[tile], points_baseline);
            hand_calculator_work.win_prob_to_work[loc_first][loc_second][mod2tn] += delta.win_prob;
            hand_calculator_work.points_exp_to_work[loc_first][loc_second][mod2tn] +=
                delta.points_exp;
            for (int han = 0; han < 5; ++han)
              hand_calculator_work.win_han_prob_to_work[loc_first][loc_second][mod2tn][han] +=
                  delta.han[han];
          }
        }
      }
    }

#pragma omp parallel
#pragma omp for
    for (int cn = 0; cn < candidates_size(); cn++) {
      for (int gn = 0; gn < group_size(cn); gn++) {
        const Hand_Location &loc = hand_calculator_work.candidates_work[cn].proto_sequence_loc[gn];
        const int loc_first = loc.get_first();
        const int loc_second = loc.get_second();
        hand_calculator_work.win_prob_work[loc_first][loc_second][mod2tn] =
            hand_calculator_work.win_prob_to_work[loc_first][loc_second][mod2tn];
        hand_calculator_work.tenpai_prob_work[loc_first][loc_second][mod2tn] =
            hand_calculator_work.tenpai_prob_to_work[loc_first][loc_second][mod2tn];
        hand_calculator_work.points_exp_work[loc_first][loc_second][mod2tn] =
            hand_calculator_work.points_exp_to_work[loc_first][loc_second][mod2tn];

        for (int han = 0; han < 5; han++) {
          hand_calculator_work.win_han_prob_work[loc_first][loc_second][mod2tn][han] =
              hand_calculator_work.win_han_prob_to_work[loc_first][loc_second][mod2tn][han];
        }

        double tenpai_tmpbest[38], exp_tmpbest[38];
        int tile_effective[38], pon_points[38];
        const simple_dp_sparsemax::PointsRecord ron_baseline = {
            hand_calculator_work.win_prob_to_work[loc_first][loc_second][mod2tn],
            hand_calculator_work.points_exp_to_work[loc_first][loc_second][mod2tn],
            hand_calculator_work.win_han_prob_to_work[loc_first][loc_second][mod2tn]};
        simple_dp_sparsemax::TileCandidates ron_candidates =
            simple_dp_sparsemax::baseline_per_tile(ron_baseline);
        for (int tile = 0; tile < 38; tile++) {
          tile_effective[tile] = 0;
          tenpai_tmpbest[tile] =
              hand_calculator_work.tenpai_prob_to_work[loc_first][loc_second][mod2tn];
          exp_tmpbest[tile] =
              hand_calculator_work.points_exp_to_work[loc_first][loc_second][mod2tn];
          pon_points[tile] = 0;
        }

        if (get_const_proto_sequence_cgn(cn, gn).get_furiten() == 0) {
          const std::array<int, 3> &win_loc = hand_calculator_work.get_const_win_loc(cn, gn);
          for (int an = win_loc[1]; an < win_loc[2]; an++) {
            const Win_Calc &win = hand_calculator_work.win_graph_work[win_loc[0]][an];
            if (win.get_points_ron(my_pid, game_state) > 0) {
              if (using_tile_kind_array[win.win_info.get_tile()] -
                      get_const_proto_sequence_cgn(cn, gn).using_tile_kind_num(
                          win.win_info.get_tile()) <
                  1) {
                if ((win.ron_exp - exp_min) > exp_tmpbest[win.win_info.get_tile()]) {
                  tile_effective[win.win_info.get_tile()] = 1;
                  exp_tmpbest[win.win_info.get_tile()] = (win.ron_exp - exp_min);
                  // TODO: Include underneath dora in this probability.
                }

                std::array<double, 5> han = {{0, 0, 0, 0, 0}};
                han[std::min(4, win.win_info.get_han_ron())] = 1.0;
                ron_candidates[win.win_info.get_tile()].push_back(
                    simple_dp_sparsemax::Candidate({1.0, (win.ron_exp - exp_min), han}, 3.0));
                tile_effective[win.win_info.get_tile()] = 1;
              }
            }
          }
        }

        const std::array<int, 3> &open_meld_edge_loc =
            hand_calculator_work.get_const_open_meld_edge_loc(cn, gn);
        for (int acn = open_meld_edge_loc[1]; acn < open_meld_edge_loc[2]; acn++) {
          const Hand_Action &open_meld_edge =
              hand_calculator_work.cand_graph_sub_open_meld_work[open_meld_edge_loc[0]][acn];
          const int dst_group = open_meld_edge.dst_group;
          const int dst_group_sub = open_meld_edge.dst_group_sub;
          const int tile = open_meld_edge.tile;
          const Hand_Location &dst_loc =
              hand_calculator_work.candidates_work[dst_group].proto_sequence_loc[dst_group_sub];
          const int dst_loc_first = dst_loc.get_first();
          const int dst_loc_second = dst_loc.get_second();
          if (hand_calculator_work.tenpai_prob_work[dst_loc_first][dst_loc_second][mod2tn_prev] >
              tenpai_tmpbest[tile]) {
            tile_effective[tile] = 1;
            tenpai_tmpbest[tile] =
                hand_calculator_work.tenpai_prob_work[dst_loc_first][dst_loc_second][mod2tn_prev];
          }
          if (open_meld_edge.action_type == AT_PON &&
              hand_calculator_work.tenpai_prob_work[dst_loc_first][dst_loc_second][mod2tn_prev] >
                  hand_calculator_work.tenpai_prob_to_work[loc_first][loc_second][mod2tn])
            pon_points[tile] = 1;
          if (hand_calculator_work.points_exp_work[dst_loc_first][dst_loc_second][mod2tn_prev] >
              exp_tmpbest[tile]) {
            tile_effective[tile] = 1;
            exp_tmpbest[tile] =
                hand_calculator_work.points_exp_work[dst_loc_first][dst_loc_second][mod2tn_prev];
          }

          const double coeff =
              (open_meld_edge.action_type == AT_PON || open_meld_edge.action_type == AT_OPEN_KAN)
                  ? 3.0
                  : 1.0;
          ron_candidates[tile].push_back(simple_dp_sparsemax::Candidate(
              {hand_calculator_work.win_prob_work[dst_loc_first][dst_loc_second][mod2tn_prev],
               hand_calculator_work.points_exp_work[dst_loc_first][dst_loc_second][mod2tn_prev],
               hand_calculator_work.win_han_prob_work[dst_loc_first][dst_loc_second][mod2tn_prev]},
              coeff));
          tile_effective[tile] = 1;
        }

        for (int tile = 0; tile < 38; tile++) {
          if (tile % 10 != 0 && tile_effective[tile] == 1) {
            double remaining_tmp = 4.0 -
                                   get_const_proto_sequence_cgn(cn, gn).using_tile_kind_num(tile) -
                                   tile_visible[tile];
            if (using_tile_kind_array[tile] -
                    get_const_proto_sequence_cgn(cn, gn).using_tile_kind_num(tile) >
                0) {
              remaining_tmp -= using_tile_kind_array[tile] -
                               get_const_proto_sequence_cgn(cn, gn).using_tile_kind_num(tile);
            }
            if (tn != tsumo_num) {
              remaining_tmp *= rn_para;
            }
            if (tenpai_tmpbest[tile] >
                hand_calculator_work.tenpai_prob_to_work[loc_first][loc_second][mod2tn]) {
              hand_calculator_work.tenpai_prob_work[loc_first][loc_second][mod2tn] +=
                  (tenpai_tmpbest[tile] -
                   hand_calculator_work.tenpai_prob_to_work[loc_first][loc_second][mod2tn]) *
                  remaining_tmp / (remaining_tile_num - (tsumo_num - tn)) *
                  (1.0 + 2.0 * pon_points[tile]);
            }

            const double event_weight = remaining_tmp / (remaining_tile_num - (tsumo_num - tn));
            for (std::size_t i = 1; i < ron_candidates[tile].size(); ++i)
              ron_candidates[tile][i].event_weight = event_weight;
            const simple_dp_sparsemax::PointsRecord delta =
                simple_dp_sparsemax::mix_delta(ron_candidates[tile], ron_baseline);
            hand_calculator_work.win_prob_work[loc_first][loc_second][mod2tn] += delta.win_prob;
            hand_calculator_work.points_exp_work[loc_first][loc_second][mod2tn] += delta.points_exp;
            for (int han = 0; han < 5; ++han)
              hand_calculator_work.win_han_prob_work[loc_first][loc_second][mod2tn][han] +=
                  delta.han[han];
          }
        }
      }
    }
  }

  clock_t check2 = clock();
  if (console_out) {
    std::cout << "cal_prob_time:" << (double)(check1 - check0) / CLOCKS_PER_SEC << " "
              << (double)(check2 - check1) / CLOCKS_PER_SEC << std::endl;
  }
}

void Hand_Calculator::calc_DP(
    const int act_num, const int tsumo_num, const double exp_other, const double exp_other_ar,
    const double exp_other_kan, const std::array<float, 4> &tenpai_prob_now,
    const std::array<std::array<float, 38>, 4> &deal_in_tile_prob,
    const std::array<std::array<float, 38>, 4> &deal_in_tile_value, const int ori_choice_mode,
    const double riichi_regression_coeff, double my_tenpai_prob, double drawn_round_prob_now,
    const Tile_Array &tile_visible_kind, const Game_State &game_state,
    const std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> &round_end_pt_exp,
    const std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> &drawn_round_pt_exp,
    const std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> &drawn_round_pt_exp_ar,
    const Tactics &tactics) {
  clock_t check0 = clock();
  set_win_exp(game_state, round_end_pt_exp);

  if (console_out) {
    std::cout << "DP_in:" << act_num << " " << tsumo_num << " " << exp_other << std::endl;
  }

  double **tenpai_prob_other, **riichi_tenpai_prob_other;
  double **deal_in_p_tile, **riichi_deal_in_p_tile;
  double **deal_in_e_tile, **riichi_deal_in_e_tile;
  double *other_end_prob, *riichi_other_end_prob;
  other_end_prob = new double[tsumo_num + 1];
  riichi_other_end_prob = new double[tsumo_num + 1];
  tenpai_prob_other = new double *[4];
  riichi_tenpai_prob_other = new double *[4];
  for (int pid = 0; pid < 4; pid++) {
    tenpai_prob_other[pid] = new double[tsumo_num + 1];
    riichi_tenpai_prob_other[pid] = new double[tsumo_num + 1];
  }
  deal_in_p_tile = new double *[tsumo_num + 1];
  deal_in_e_tile = new double *[tsumo_num + 1];
  riichi_deal_in_p_tile = new double *[tsumo_num + 1];
  riichi_deal_in_e_tile = new double *[tsumo_num + 1];
  for (int tn = 0; tn <= tsumo_num; tn++) {
    deal_in_p_tile[tn] = new double[38];
    deal_in_e_tile[tn] = new double[38];
    riichi_deal_in_p_tile[tn] = new double[38];
    riichi_deal_in_e_tile[tn] = new double[38];
  }

  set_tenpai_prob_other(my_pid, game_state, tsumo_num, tenpai_prob_now, tenpai_prob_other,
                        riichi_tenpai_prob_other);

  double exp_drawn_round[2] = {};
  double exp_drawn_round_ar = 0.0;

  set_exp_drawn_round_DP(my_pid, tenpai_prob_other, riichi_tenpai_prob_other, drawn_round_pt_exp,
                         drawn_round_pt_exp_ar, exp_drawn_round, exp_drawn_round_ar);

  if (console_out) {
    std::cout << "exp_drawn_round:" << exp_drawn_round[0] << " " << exp_drawn_round[1] << " "
              << exp_drawn_round_ar << std::endl;
  }

  for (int tn = 0; tn < tsumo_num + 1; tn++) {
    for (int tile = 0; tile < 38; tile++) {
      deal_in_p_tile[tn][tile] = 0.0;
      deal_in_e_tile[tn][tile] = 0.0;
      riichi_deal_in_p_tile[tn][tile] = 0.0;
      riichi_deal_in_e_tile[tn][tile] = 0.0;
    }
    for (int tile = 1; tile < 38; tile++) {
      for (int pid = 0; pid < 4; pid++) {
        if (pid != my_pid) {
          deal_in_p_tile[tn][tile] += tenpai_prob_other[pid][tn] * deal_in_tile_prob[pid][tile];
          deal_in_e_tile[tn][tile] += tenpai_prob_other[pid][tn] * deal_in_tile_prob[pid][tile] *
                                      deal_in_tile_value[pid][tile];
          riichi_deal_in_p_tile[tn][tile] +=
              riichi_tenpai_prob_other[pid][tn] * deal_in_tile_prob[pid][tile];
          riichi_deal_in_e_tile[tn][tile] += riichi_tenpai_prob_other[pid][tn] *
                                             deal_in_tile_prob[pid][tile] *
                                             deal_in_tile_value[pid][tile];
        }
      }
    }
  }

  set_other_end_prob(my_pid, tsumo_num, act_num, tenpai_prob_now, other_end_prob,
                     riichi_other_end_prob, game_state);

  if (console_out) {
    std::cout << "tenpai_prob_DP:" << act_num << std::endl;
    for (int tn = 0; tn <= tsumo_num; tn++) {
      std::cout << other_end_prob[tn] << " ";
      for (int pid = 0; pid < 4; pid++) {
        if (pid != my_pid) {
          std::cout << tenpai_prob_other[pid][tn] << " ";
        }
      }
      std::cout << "- " << riichi_other_end_prob[tn] << " ";
      for (int pid = 0; pid < 4; pid++) {
        if (pid != my_pid) {
          std::cout << riichi_tenpai_prob_other[pid][tn] << " ";
        }
      }
      std::cout << std::endl;
    }
  }

  exec_calc_DP(riichi_regression_coeff, ori_choice_mode, my_pid, tsumo_num, hand_calculator_work,
               drawn_round_prob_now, exp_drawn_round, exp_drawn_round_ar, exp_drawn_round, false,
               deal_in_p_tile, riichi_deal_in_p_tile, deal_in_e_tile, riichi_deal_in_e_tile,
               other_end_prob, riichi_other_end_prob, exp_other, exp_other_ar, exp_other_kan,
               my_tenpai_prob, deal_in_tile_prob, tenpai_prob_other, game_state, tactics);

  for (int pid = 0; pid < 4; pid++) {
    delete[] tenpai_prob_other[pid];
    delete[] riichi_tenpai_prob_other[pid];
  }
  delete[] tenpai_prob_other;
  delete[] riichi_tenpai_prob_other;

  for (int tn = 0; tn <= tsumo_num; tn++) {
    delete[] deal_in_p_tile[tn];
    delete[] deal_in_e_tile[tn];
    delete[] riichi_deal_in_p_tile[tn];
    delete[] riichi_deal_in_e_tile[tn];
  }
  delete[] deal_in_p_tile;
  delete[] deal_in_e_tile;
  delete[] riichi_deal_in_p_tile;
  delete[] riichi_deal_in_e_tile;

  delete[] other_end_prob;
  delete[] riichi_other_end_prob;

  clock_t check1 = clock();
  if (console_out) {
    std::cout << "cal_prob_time:" << (double)(check1 - check0) / CLOCKS_PER_SEC << std::endl;
  }
}
