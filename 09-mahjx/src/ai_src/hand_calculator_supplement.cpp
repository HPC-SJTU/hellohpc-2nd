#include "hand_calculator_supplement.hpp"

#include "params.hpp"
#include "sparsemax_bellman.hpp"

extern std::array<Tactics, 4> tactics_all;

int mod2(const int x) { return x % 2; }

bool same_element_exist(const std::vector<int> &vec1, const std::vector<int> &vec2) {
  for (int i1 = 0; i1 < vec1.size(); i1++) {
    for (int i2 = 0; i2 < vec2.size(); i2++) {
      if (vec1[i1] == vec2[i2]) {
        return true;
      }
    }
  }
  return false;
}

void set_tsumo_node_exec(
    Hand_Change &hand_change_new, bool is_in0,
    boost::container::static_vector<Hand_Group, MAX_CANDIDATES_NUM> &candidates_work,
    boost::unordered_set<Hand_Change> &tcs, int &in0num) {
  if (candidates_work.size() >= MAX_CANDIDATES_NUM) {
    // Do we need to output an alert?
    return;
  }

  hand_change_new.id = candidates_work.size();
  // Assigning an id to hand_change can be clearer with a map. Reconsider this.
  const std::pair<boost::unordered_set<Hand_Change>::iterator, bool> ret =
      tcs.insert(hand_change_new);
  if (ret.second) {
    if (is_in0) {
      in0num++;
    }
    Hand_Group cand_group;
    cand_group.hand_change = hand_change_new;
    candidates_work.push_back(cand_group);
  }
}

void set_tsumo_node_child(
    const Bit_Tile_Num &bit_tile_num, const int hand_all_num, std::vector<int> &tile_in_pattern_nip,
    std::vector<int> &remain, int nin,
    boost::container::static_vector<Hand_Group, MAX_CANDIDATES_NUM> &candidates_work,
    boost::unordered_set<Hand_Change> &tcs, int &in0num) {
  Hand_Change hand_change_tmp_new;
  hand_change_tmp_new.hand_base = bit_tile_num;
  const int nout = hand_all_num - 13 + nin;
  do {
    for (int i = 0; i < nin; i++) {
      hand_change_tmp_new.hand_base.add_tile(tile_in_pattern_nip[i]);
    }
    do {
      for (int i = 0; i < nout; i++) {
        hand_change_tmp_new.hand_base.delete_tile(remain[i]);
      }
      set_tsumo_node_exec(hand_change_tmp_new, false, candidates_work, tcs, in0num);
      for (int i = 0; i < nout; i++) {
        hand_change_tmp_new.hand_base.add_tile(remain[i]);
      }
    } while (boost::next_combination(remain.begin(), remain.begin() + nout, remain.end()));
    for (int i = 0; i < nin; i++) {
      hand_change_tmp_new.hand_base.delete_tile(tile_in_pattern_nip[i]);
    }
  } while (boost::next_combination(tile_in_pattern_nip.begin(), tile_in_pattern_nip.begin() + nin,
                                   tile_in_pattern_nip.end()));
}

void set_tsumo_node_meld(
    const Bit_Tile_Num &bit_tile_num, const int hand_all_num, Hand_Analyzer proto_sequence_in,
    const int open_meld_cand_tile, const int in_num_end,
    boost::container::static_vector<Hand_Group, MAX_CANDIDATES_NUM> &candidates_work,
    boost::unordered_set<Hand_Change> &tcs, int &in0num) {
  for (int nin = 1; nin <= in_num_end; nin++) {
    // If you parallelize this for loop, you must lock mtx in set_tsumo_node_exec.
    for (int nip = 0; nip < proto_sequence_in.inout_pattern_vec[in_num_end].size(); nip++) {
      if (open_meld_cand_tile != 0 &&
          proto_sequence_in.get_hand_num() + proto_sequence_in.get_open_meld_num() * 3 == 14) {
        proto_sequence_in.inout_pattern_vec[in_num_end][nip].tile_in_pattern.push_back(
            tile_kind(open_meld_cand_tile));
        std::sort(proto_sequence_in.inout_pattern_vec[in_num_end][nip].tile_in_pattern.begin(),
                  proto_sequence_in.inout_pattern_vec[in_num_end][nip].tile_in_pattern.end());
      }
      if (same_element_exist(
              proto_sequence_in.inout_pattern_vec[in_num_end][nip].tile_in_pattern,
              proto_sequence_in.inout_pattern_vec[in_num_end][nip].tile_out_pattern) == 0) {
        set_tsumo_node_child(bit_tile_num, hand_all_num,
                             proto_sequence_in.inout_pattern_vec[in_num_end][nip].tile_in_pattern,
                             proto_sequence_in.inout_pattern_vec[in_num_end][nip].tile_out_pattern,
                             nin, candidates_work, tcs, in0num);
      }
      if (open_meld_cand_tile != 0 &&
          proto_sequence_in.get_hand_num() + proto_sequence_in.get_open_meld_num() * 3 == 14) {
        proto_sequence_in.inout_pattern_vec[in_num_end][nip].tile_in_pattern.erase(
            proto_sequence_in.inout_pattern_vec[in_num_end][nip].tile_in_pattern.begin() +
            loc_intvec(proto_sequence_in.inout_pattern_vec[in_num_end][nip].tile_in_pattern,
                       tile_kind(open_meld_cand_tile)));
      }
    }
  }
}

void set_tenpai_prob_other(const int my_pid, const Game_State &game_state, const int tsumo_num,
                           const std::array<float, 4> &tenpai_prob_now, double **tenpai_prob_other,
                           double **riichi_tenpai_prob_other) {
  {
    const int act_num = game_state.player_state[my_pid].river.size();
    const int after_begin = tactics_all[my_pid].tenpai_after_est_begin;
    const int other_riichi_declared_num = get_other_riichi_declared_num(my_pid, game_state);
    for (int pid = 0; pid < 4; pid++) {
      for (int tn = 0; tn <= tsumo_num; tn++) {
        if (game_state.player_state[pid].riichi_declared) {
          tenpai_prob_other[pid][tsumo_num - tn] = 1.0;
          riichi_tenpai_prob_other[pid][tsumo_num - tn] = 1.0;
        } else {
          tenpai_prob_other[pid][tsumo_num - tn] = cal_tenpai_after_prob(
              false, act_num, act_num + after_begin + tn, tenpai_prob_now[pid]);
          riichi_tenpai_prob_other[pid][tsumo_num - tn] = cal_tenpai_after_prob(
              true, act_num, act_num + after_begin + tn, tenpai_prob_now[pid]);
        }
        // TODO: Include this adjustment in the kanon model.
        if (0 < other_riichi_declared_num) {
          tenpai_prob_other[pid][tsumo_num - tn] = riichi_tenpai_prob_other[pid][tsumo_num - tn];
        }
      }
    }
  }
}

float cal_other_end_prob(const bool my_riichi, const int act_num1, const int act_num2,
                         const int other_riichi_num, const float other_end_prob_input) {
  const int lb = my_riichi ? 7 : 6;
  const int ub = my_riichi ? 15 : 16;
  const int ac1 = std::min(std::max(lb, act_num1), ub);
  const int ac2 = std::min(std::max(lb, act_num2), ub);
  const float *const params =
      my_riichi ? RIICHI_OTHER_END_PARA[ac1][ac2] : OTHER_END_PARA[ac1][ac2];
  float w[3];
  for (int i = 0; i < 3; i++) w[i] = params[i];

  float x[3];
  x[0] = 1.0;
  x[1] = other_riichi_num;
  x[2] = other_end_prob_input;
  return logistic(w, x, 3);
}

float cal_tenpai_after_prob(const bool my_riichi, const int act_num1, const int act_num2,
                            const float tenpai_prob_now) {
  if (act_num2 <= act_num1) {
    return tenpai_prob_now;
  }

  const float *const params = my_riichi
                                  ? RIICHI_TENPAI_AFTER_PARA[std::min(std::max(1, act_num1), 15)]
                                                            [std::min(std::max(3, act_num2), 18)]
                                  : TENPAI_AFTER_PARA[std::min(std::max(1, act_num1), 17)]
                                                     [std::min(std::max(2, act_num2), 18)];

  float w[2];
  for (int i = 0; i < 2; i++) w[i] = params[i];

  float x[2];
  x[0] = 1.0;
  x[1] = my_logit(tenpai_prob_now);
  return logistic(w, x, 2);
}

void set_other_end_prob(const int my_pid, const int tsumo_num, const int act_num,
                        const std::array<float, 4> &tenpai_prob_now, double *other_end_prob,
                        double *riichi_other_end_prob, const Game_State &game_state) {
  {
    float other_end_prob_input = 1.0;
    for (int pid = 0; pid < 4; pid++) {
      if (pid != my_pid && !game_state.player_state[pid].riichi_declared) {
        other_end_prob_input *= (1.0 - tenpai_prob_now[pid]);
      }
    }
    other_end_prob_input = 1.0 - other_end_prob_input;
    for (int tn = 0; tn <= tsumo_num; tn++) {
      other_end_prob[tsumo_num - tn] = cal_other_end_prob(
          false, act_num, act_num + tn, get_other_riichi_declared_num(my_pid, game_state),
          other_end_prob_input);
      riichi_other_end_prob[tsumo_num - tn] = cal_other_end_prob(
          true, act_num, act_num + tn, get_other_riichi_declared_num(my_pid, game_state),
          other_end_prob_input);
    }
  }
}

void set_exp_drawn_round_DP(
    const int my_pid, const double *const *tenpai_prob_other,
    const double *const *riichi_tenpai_prob_other,
    const std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> &drawn_round_pt_exp,
    const std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> &drawn_round_pt_exp_ar,
    double exp_drawn_round[2], double &exp_drawn_round_ar) {
  int flag_tmp[4];
  for (int t0 = 0; t0 < 2; t0++) {
    for (int t1 = 0; t1 < 2; t1++) {
      for (int t2 = 0; t2 < 2; t2++) {
        for (int t3 = 0; t3 < 2; t3++) {
          flag_tmp[0] = t0;
          flag_tmp[1] = t1;
          flag_tmp[2] = t2;
          flag_tmp[3] = t3;
          double prob_tmp = 1.0;
          for (int pid = 0; pid < 4; pid++) {
            if (pid != my_pid) {
              if (flag_tmp[pid] == 1) {
                prob_tmp *= tenpai_prob_other[pid][0];
              } else {
                prob_tmp *= 1.0 - tenpai_prob_other[pid][0];
              }
            }
          }
          exp_drawn_round[flag_tmp[my_pid]] += drawn_round_pt_exp[t0][t1][t2][t3] * prob_tmp;
          if (flag_tmp[my_pid] == 1) {
            prob_tmp = 1.0;
            for (int pid = 0; pid < 4; pid++) {
              if (pid != my_pid) {
                if (flag_tmp[pid] == 1) {
                  prob_tmp *= riichi_tenpai_prob_other[pid][0];
                } else {
                  prob_tmp *= 1.0 - riichi_tenpai_prob_other[pid][0];
                }
              }
            }
            exp_drawn_round_ar += drawn_round_pt_exp_ar[t0][t1][t2][t3] * prob_tmp;
          }
        }
      }
    }
  }
}

double cal_fold_exp(const int my_pid, const int cn, const int gn, const int tn, const float wa[3],
                    Hand_Calculator_Work &hand_calculator_work, double **deal_in_p_tile,
                    double **deal_in_e_tile, double exp_other, const int tsumo_num,
                    const int fold_choice_mode, double drawn_round_prob,
                    double not_ready_drawn_round_value, const Tile_Array hand_kind,
                    const Open_Meld_Vector open_meld_kind) {
  Tile_Array using_tile_kind_array = using_tile_array(hand_kind, open_meld_kind);
  const Hand_Location &loc = hand_calculator_work.candidates_work[cn].proto_sequence_loc[gn];
  const int loc_first = loc.get_first();
  const int loc_second = loc.get_second();
  std::array<float, 38> deal_in_prob = {};
  std::array<float, 38> deal_in_value = {};
  for (int tile = 1; tile < 38; tile++) {
    const int kind = tile % 10 == 0 ? tile - 5 : tile;
    const int root_use = using_tile_kind_array[kind];
    const int state_use = hand_calculator_work.cal_proto_sequence_value_work[loc_first][loc_second]
                              .using_tile_kind_num(kind);
    deal_in_prob[tile] = deal_in_p_tile[tn][tile];
    if (root_use - state_use > 0) {
      deal_in_prob[tile] = 0.0;
    }
    if (deal_in_prob[tile] > 0.0) {
      deal_in_value[tile] = deal_in_e_tile[tn][tile] / deal_in_prob[tile];
    }
  }
  Full_Defense full_defense;

  full_defense.set_condition(deal_in_prob, deal_in_value, exp_other, not_ready_drawn_round_value);

  full_defense.set_hand_proto_sequence(
      hand_calculator_work.cal_proto_sequence_value_work[loc_first][loc_second]);
  full_defense.set_full_defense_value(tn);
  if (fold_choice_mode == 2) {
    if (hand_calculator_work.cal_proto_sequence_value_work[loc_first][loc_second]
            .get_open_meld_num() < 2) {
      full_defense.modify_full_defense_value_with_drawn_round(
          hand_calculator_work.cal_proto_sequence_value_work[loc_first][loc_second]
              .get_open_meld_num(),
          wa, drawn_round_prob);
    }
  }
  return full_defense.full_defense_exp;
}

float get_coeff_for_ron_DP(const int tile, const int my_pid,
                           const Hand_Analyzer_Basic &proto_sequence, const int pon_ron[38],
                           const int is_ron[38], const double my_tenpai_prob,
                           const std::array<std::array<float, 38>, 4> &deal_in_tile_prob,
                           double **tenpai_prob_other, const int tn,
                           const float riichi_ron_ratio_para[5],
                           const float not_riichi_ron_ratio_para[5],
                           const float hidden_tenpai_ron_ratio[6]) {
  float get_coeff_reg = 1.0 + 2.0 * pon_ron[tile];
  if (is_ron[tile] == 1) {
    if (proto_sequence.get_open_meld_num() == 0 && proto_sequence.get_riichi() == 0) {
      if (tile > 30) {
        get_coeff_reg = hidden_tenpai_ron_ratio[0] / (1.0 - hidden_tenpai_ron_ratio[0]);
      } else {
        int tmpi = 5 - abs(tile % 10 - 5);
        get_coeff_reg = hidden_tenpai_ron_ratio[tmpi] / (1.0 - hidden_tenpai_ron_ratio[tmpi]);
      }
    } else {
      float x[3] = {};
      float tmp;
      x[0] = 1.0;
      for (int pid = 0; pid < 4; pid++) {
        if (pid != my_pid) {
          x[2] += tenpai_prob_other[pid][tn];
        }
      }
      if (proto_sequence.get_riichi() == 1) {
        x[1] = deal_in_tile_prob[my_pid][tile];
        tmp = logistic(riichi_ron_ratio_para, x, 3);
      } else {
        x[1] = my_tenpai_prob * deal_in_tile_prob[my_pid][tile];
        tmp = logistic(not_riichi_ron_ratio_para, x, 3);
      }
      if (tmp != 1.0) {
        get_coeff_reg = tmp / (1.0 - tmp);
      }
    }
  }
  return get_coeff_reg;
}

void exec_calc_DP(const double riichi_regression_coeff, const int fold_choice_mode,
                  const int my_pid, const int tsumo_num, Hand_Calculator_Work &hand_calculator_work,
                  const double drawn_round_prob_now, const double exp_drawn_round[2],
                  const double exp_drawn_round_ar, const double exp_drawn_round_if_open_meld[2],
                  const bool is_last_mode, double **deal_in_p_tile, double **riichi_deal_in_p_tile,
                  double **deal_in_e_tile, double **riichi_deal_in_e_tile, double *other_end_prob,
                  double *riichi_other_end_prob, const double exp_other, const double exp_other_ar,
                  const double exp_other_kan, const double my_tenpai_prob,
                  const std::array<std::array<float, 38>, 4> &deal_in_tile_prob,
                  double **tenpai_prob_other, const Game_State &game_state,
                  const Tactics &tactics) {
  const std::array<
      boost::container::static_vector<Hand_Analyzer_Basic, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
      CAL_NUM_THREAD> &cal_proto_sequence_value =
      hand_calculator_work.cal_proto_sequence_value_work;
  const boost::container::static_vector<Hand_Group, MAX_CANDIDATES_NUM> &candidates =
      hand_calculator_work.candidates_work;
  const Tile_Array hand_kind = tile_kind(game_state.player_state[my_pid].hand);
  const Open_Meld_Vector open_meld_kind = tile_kind(game_state.player_state[my_pid].open_meld);

  const int remaining_tile_num = get_remaining_tile_num(my_pid, game_state);
  const Tile_Array tile_visible = tile_kind(get_tile_visible_wo_hand(my_pid, game_state));
  const Tile_Array using_tile_kind_array = tile_kind(using_tile_array(
      game_state.player_state[my_pid].hand, game_state.player_state[my_pid].open_meld));
  for (int cn = 0; cn < candidates.size(); cn++) {
    for (int gn = 0; gn < candidates[cn].proto_sequence_loc.size(); gn++) {
      const Hand_Location &loc = candidates[cn].proto_sequence_loc[gn];
      const int loc_first = loc.get_first();
      const int loc_second = loc.get_second();
      hand_calculator_work.win_prob_work[loc_first][loc_second][0] = 0.0;
      hand_calculator_work.win_prob_to_work[loc_first][loc_second][0] = 0.0;
      hand_calculator_work.tenpai_prob_work[loc_first][loc_second][0] =
          1.0 * cal_proto_sequence_value[loc_first][loc_second].get_tenpai();
      hand_calculator_work.tenpai_prob_to_work[loc_first][loc_second][0] =
          1.0 * cal_proto_sequence_value[loc_first][loc_second].get_tenpai();
      hand_calculator_work.win_exp_work[loc_first][loc_second][0] = 0.0;
      hand_calculator_work.win_exp_to_work[loc_first][loc_second][0] = 0.0;
      if (cal_proto_sequence_value[loc_first][loc_second].get_riichi() == 1) {
        hand_calculator_work.points_exp_work[loc_first][loc_second][0] = exp_drawn_round_ar;
        hand_calculator_work.points_exp_to_work[loc_first][loc_second][0] = exp_drawn_round_ar;
      } else if (cal_proto_sequence_value[loc_first][loc_second].get_tenpai() == 1) {
        hand_calculator_work.points_exp_work[loc_first][loc_second][0] = exp_drawn_round[1];
        hand_calculator_work.points_exp_to_work[loc_first][loc_second][0] = exp_drawn_round[1];
      } else {
        hand_calculator_work.points_exp_work[loc_first][loc_second][0] = exp_drawn_round[0];
        hand_calculator_work.points_exp_to_work[loc_first][loc_second][0] = exp_drawn_round[0];
      }
    }
  }

  // These parameters are unused except in the reference implementation.
  float riichi_ron_ratio_para[5] = {};
  float not_riichi_ron_ratio_para[5] = {};
  float hidden_tenpai_ron_ratio[6] = {};
  float wa[MAX_TSUMO_NUM][3];

  {
    for (int i = 0; i < 3; i++) riichi_ron_ratio_para[i] = RIICHI_RON_RATIO[i];
    for (int i = 0; i < 3; i++) not_riichi_ron_ratio_para[i] = NOT_RIICHI_RON_RATIO[i];
    for (int i = 0; i < 6; i++) hidden_tenpai_ron_ratio[i] = HIDDEN_TENPAI_RON_RATIO[i];
  }
  {
    for (int t = 2; t <= 18; t++) {
      const float *const params = INCLUSIVE_FOLD_PARA[t];
      for (int i = 0; i < 3; i++) wa[t][i] = params[i];
    }
  }

  for (int tn = 1; tn <= tsumo_num; tn++) {
    const int mod2tn = mod2(tn);
    const int mod2tn_prev = mod2(tn - 1);
    const double drawn_round_prob = 1.0 + tn * (drawn_round_prob_now - 1.0) / tsumo_num;
    if (fold_choice_mode > 0) {
      {
        const int model_idx = std::min(
            std::max(2, (int)game_state.player_state[my_pid].river.size() + tsumo_num - tn), 18);
#pragma omp parallel
#pragma omp for
        for (int cn = 0; cn < candidates.size(); cn++) {
          for (int gn = 0; gn < candidates[cn].proto_sequence_loc.size(); gn++) {
            const Hand_Location &loc =
                hand_calculator_work.candidates_work[cn].proto_sequence_loc[gn];
            hand_calculator_work.fold_exp_work[loc.get_first()][loc.get_second()] =
                cal_fold_exp(my_pid, cn, gn, tn, wa[model_idx], hand_calculator_work,
                             deal_in_p_tile, deal_in_e_tile, exp_other, tsumo_num, fold_choice_mode,
                             drawn_round_prob, exp_drawn_round[0], hand_kind, open_meld_kind);
          }
        }
      }
    }

#pragma omp parallel
#pragma omp for
    for (int cn = 0; cn < candidates.size(); cn++) {
      for (int gn = 0; gn < candidates[cn].proto_sequence_loc.size(); gn++) {
        const Hand_Location &loc = hand_calculator_work.candidates_work[cn].proto_sequence_loc[gn];
        const int loc_first = loc.get_first();
        const int loc_second = loc.get_second();

        hand_calculator_work.win_prob_to_work[loc_first][loc_second][mod2tn] = 0.0;
        hand_calculator_work.tenpai_prob_to_work[loc_first][loc_second][mod2tn] = 0.0;
        hand_calculator_work.win_exp_to_work[loc_first][loc_second][mod2tn] = 0.0;
        hand_calculator_work.points_exp_to_work[loc_first][loc_second][mod2tn] = 0.0;

        double prob_tmpbest[38], tenpai_tmpbest[38], win_exp_tmpbest[38], exp_tmpbest[38];
        std::vector<sparsemax_bellman::Candidate> tsumo_candidates[38];
        for (int tile = 0; tile < 38; tile++) {
          if (tile % 10 != 0) {
            if (cal_proto_sequence_value[loc_first][loc_second].get_riichi() == 1) {
              prob_tmpbest[tile] =
                  (1.0 - riichi_deal_in_p_tile[tn - 1][tile]) *
                  hand_calculator_work.win_prob_work[loc_first][loc_second][mod2tn_prev];
              tenpai_tmpbest[tile] =
                  (1.0 - riichi_deal_in_p_tile[tn - 1][tile]) *
                  hand_calculator_work.tenpai_prob_work[loc_first][loc_second][mod2tn_prev];
              win_exp_tmpbest[tile] =
                  (1.0 - riichi_deal_in_p_tile[tn - 1][tile]) *
                  hand_calculator_work.win_exp_work[loc_first][loc_second][mod2tn_prev];
              exp_tmpbest[tile] =
                  (1.0 - riichi_deal_in_p_tile[tn - 1][tile]) *
                      hand_calculator_work.points_exp_work[loc_first][loc_second][mod2tn_prev] +
                  riichi_deal_in_e_tile[tn - 1][tile];
            } else {
              prob_tmpbest[tile] =
                  (1.0 - deal_in_p_tile[tn - 1][tile]) *
                  hand_calculator_work.win_prob_work[loc_first][loc_second][mod2tn_prev];
              tenpai_tmpbest[tile] =
                  (1.0 - deal_in_p_tile[tn - 1][tile]) *
                  hand_calculator_work.tenpai_prob_work[loc_first][loc_second][mod2tn_prev];
              win_exp_tmpbest[tile] =
                  (1.0 - deal_in_p_tile[tn - 1][tile]) *
                  hand_calculator_work.win_exp_work[loc_first][loc_second][mod2tn_prev];
              exp_tmpbest[tile] =
                  (1.0 - deal_in_p_tile[tn - 1][tile]) *
                      hand_calculator_work.points_exp_work[loc_first][loc_second][mod2tn_prev] +
                  deal_in_e_tile[tn - 1][tile];
            }
            tsumo_candidates[tile].push_back({{prob_tmpbest[tile], win_exp_tmpbest[tile],
                                               tenpai_tmpbest[tile], exp_tmpbest[tile]},
                                              1.0});
          }
        }
        const std::array<int, 3> &win_loc = hand_calculator_work.get_const_win_loc(cn, gn);
        for (int an = win_loc[1]; an < win_loc[2]; an++) {
          const Win_Calc &win = hand_calculator_work.win_graph_work[win_loc[0]][an];
          if (win.get_points_tsumo(my_pid, game_state) > 0) {
            // Positive tau sees every terminal exactly once.  The arrays are
            // still the unmodified legacy path when tau is zero.
            if (sparsemax_bellman::kTauPoints > 0.0) {
              tsumo_candidates[win.win_info.get_tile()].push_back(
                  sparsemax_bellman::Candidate({1.0, win.tsumo_exp, 0.0, win.tsumo_exp}));
            }
          }
        }

        const std::array<int, 3> &tsumo_edge_loc =
            hand_calculator_work.get_const_tsumo_edge_loc(cn, gn);
        for (int acn = tsumo_edge_loc[1]; acn < tsumo_edge_loc[2]; acn++) {
          const Hand_Action &ac_tmp =
              hand_calculator_work.cand_graph_sub_tsumo_work[tsumo_edge_loc[0]][acn];
          const int &tile = ac_tmp.tile;
          const int &tile_out = ac_tmp.tile_out;
          const int &dst_group = ac_tmp.dst_group;
          const int &dst_group_sub = ac_tmp.dst_group_sub;
          const Hand_Location &dst_loc =
              hand_calculator_work.candidates_work[dst_group].proto_sequence_loc[dst_group_sub];
          const int dst_loc_first = dst_loc.get_first();
          const int dst_loc_second = dst_loc.get_second();

          if (cal_proto_sequence_value[dst_loc_first][dst_loc_second].get_negative() == 0) {
            sparsemax_bellman::Candidate edge;
            edge.record = {
                (1.0 - deal_in_p_tile[tn - 1][tile_out]) *
                    hand_calculator_work.win_prob_work[dst_loc_first][dst_loc_second][mod2tn_prev],
                (1.0 - deal_in_p_tile[tn - 1][tile_out]) *
                    hand_calculator_work.win_exp_work[dst_loc_first][dst_loc_second][mod2tn_prev],
                (1.0 - deal_in_p_tile[tn - 1][tile_out]) *
                    hand_calculator_work
                        .tenpai_prob_work[dst_loc_first][dst_loc_second][mod2tn_prev],
                (1.0 - deal_in_p_tile[tn - 1][tile_out]) *
                        hand_calculator_work
                            .points_exp_work[dst_loc_first][dst_loc_second][mod2tn_prev] +
                    deal_in_e_tile[tn - 1][tile_out]};
            if (sparsemax_bellman::kTauPoints > 0.0) tsumo_candidates[tile].push_back(edge);
          }
        }

        // Project the complete baseline + terminal + edge set once per draw
        // tile.  No pairwise blend or traversal-order-dependent relaxation.
        if (sparsemax_bellman::kTauPoints > 0.0) {
          for (int tile = 1; tile < 38; ++tile) {
            if (tile % 10 == 0 || tsumo_candidates[tile].empty()) continue;
            const sparsemax_bellman::Record r = sparsemax_bellman::mix_all(tsumo_candidates[tile]);
            sparsemax_bellman::assign(r, prob_tmpbest[tile], win_exp_tmpbest[tile],
                                      tenpai_tmpbest[tile], exp_tmpbest[tile]);
          }
        }

        double remaining_all2 = 0;
        for (int tile = 0; tile < 38; tile++) {
          if (tile % 10 != 0) {
            double remaining_tmp =
                4 - cal_proto_sequence_value[loc_first][loc_second].using_tile_kind_num(tile) -
                tile_visible[tile];
            if (using_tile_kind_array[tile] -
                    cal_proto_sequence_value[loc_first][loc_second].using_tile_kind_num(tile) >
                0) {
              remaining_tmp -=
                  using_tile_kind_array[tile] -
                  cal_proto_sequence_value[loc_first][loc_second].using_tile_kind_num(tile);
            }
            remaining_all2 += remaining_tmp;

            hand_calculator_work.tenpai_prob_to_work[loc_first][loc_second][mod2tn] +=
                tenpai_tmpbest[tile] * remaining_tmp;
            hand_calculator_work.win_prob_to_work[loc_first][loc_second][mod2tn] +=
                prob_tmpbest[tile] * remaining_tmp;
            hand_calculator_work.win_exp_to_work[loc_first][loc_second][mod2tn] +=
                win_exp_tmpbest[tile] * remaining_tmp;
            hand_calculator_work.points_exp_to_work[loc_first][loc_second][mod2tn] +=
                exp_tmpbest[tile] * remaining_tmp;
          }
        }
        hand_calculator_work.tenpai_prob_to_work[loc_first][loc_second][mod2tn] =
            hand_calculator_work.tenpai_prob_to_work[loc_first][loc_second][mod2tn] /
            remaining_all2;
        hand_calculator_work.win_prob_to_work[loc_first][loc_second][mod2tn] =
            hand_calculator_work.win_prob_to_work[loc_first][loc_second][mod2tn] / remaining_all2;
        hand_calculator_work.win_exp_to_work[loc_first][loc_second][mod2tn] =
            hand_calculator_work.win_exp_to_work[loc_first][loc_second][mod2tn] / remaining_all2;
        hand_calculator_work.points_exp_to_work[loc_first][loc_second][mod2tn] =
            hand_calculator_work.points_exp_to_work[loc_first][loc_second][mod2tn] / remaining_all2;

        if (fold_choice_mode > 0 &&
            cal_proto_sequence_value[loc_first][loc_second].get_riichi() == 0) {
          if (tactics.fold_exp_at_dp_open_meld ||
              cal_proto_sequence_value[loc_first][loc_second].get_open_meld_num() ==
                  game_state.player_state[my_pid].open_meld.size()) {
            const sparsemax_bellman::Record play = {
                hand_calculator_work.win_prob_to_work[loc_first][loc_second][mod2tn],
                hand_calculator_work.win_exp_to_work[loc_first][loc_second][mod2tn],
                hand_calculator_work.tenpai_prob_to_work[loc_first][loc_second][mod2tn],
                hand_calculator_work.points_exp_to_work[loc_first][loc_second][mod2tn]};
            const sparsemax_bellman::Record fold = {
                0.0, 0.0, 0.0, hand_calculator_work.fold_exp_work[loc_first][loc_second]};
            std::vector<sparsemax_bellman::Candidate> choices;
            choices.push_back(sparsemax_bellman::Candidate(play));
            choices.push_back(sparsemax_bellman::Candidate(fold));
            sparsemax_bellman::assign(
                sparsemax_bellman::mix_all(choices),
                hand_calculator_work.win_prob_to_work[loc_first][loc_second][mod2tn],
                hand_calculator_work.win_exp_to_work[loc_first][loc_second][mod2tn],
                hand_calculator_work.tenpai_prob_to_work[loc_first][loc_second][mod2tn],
                hand_calculator_work.points_exp_to_work[loc_first][loc_second][mod2tn]);
          }
        }
      }
    }

#pragma omp parallel
#pragma omp for
    for (int cn = 0; cn < candidates.size(); cn++) {
      for (int gn = 0; gn < candidates[cn].proto_sequence_loc.size(); gn++) {
        const Hand_Location &loc = hand_calculator_work.candidates_work[cn].proto_sequence_loc[gn];
        const int loc_first = loc.get_first();
        const int loc_second = loc.get_second();
        hand_calculator_work.win_prob_work[loc_first][loc_second][mod2tn] =
            hand_calculator_work.win_prob_to_work[loc_first][loc_second][mod2tn];
        hand_calculator_work.win_exp_work[loc_first][loc_second][mod2tn] =
            hand_calculator_work.win_exp_to_work[loc_first][loc_second][mod2tn];
        hand_calculator_work.tenpai_prob_work[loc_first][loc_second][mod2tn] =
            hand_calculator_work.tenpai_prob_to_work[loc_first][loc_second][mod2tn];
        hand_calculator_work.points_exp_work[loc_first][loc_second][mod2tn] =
            hand_calculator_work.points_exp_to_work[loc_first][loc_second][mod2tn];

        // A discarded tile is one event, so each tile gets an independent
        // complete simplex: baseline + all ron terminals + all open-meld edges.
        if (sparsemax_bellman::kTauPoints > 0.0) {
          const sparsemax_bellman::Record baseline = {
              hand_calculator_work.win_prob_to_work[loc_first][loc_second][mod2tn],
              hand_calculator_work.win_exp_to_work[loc_first][loc_second][mod2tn],
              hand_calculator_work.tenpai_prob_to_work[loc_first][loc_second][mod2tn],
              hand_calculator_work.points_exp_to_work[loc_first][loc_second][mod2tn]};
          std::vector<sparsemax_bellman::Candidate> ron_candidates[38];
          for (int tile = 1; tile < 38; ++tile) {
            if (tile % 10 != 0)
              ron_candidates[tile].push_back(sparsemax_bellman::Candidate(baseline, 1.0, 0.0));
          }
          const std::array<int, 3> &win_loc = hand_calculator_work.get_const_win_loc(cn, gn);
          if (cal_proto_sequence_value[loc_first][loc_second].get_furiten() == 0) {
            for (int an = win_loc[1]; an < win_loc[2]; ++an) {
              const Win_Calc &win = hand_calculator_work.win_graph_work[win_loc[0]][an];
              const int tile = win.win_info.get_tile();
              if (win.get_points_ron(my_pid, game_state) <= 0 ||
                  using_tile_kind_array[tile] -
                          cal_proto_sequence_value[loc_first][loc_second].using_tile_kind_num(
                              tile) >=
                      1)
                continue;
              int candidate_pon_ron[38] = {}, candidate_is_ron[38] = {};
              candidate_pon_ron[tile] = candidate_is_ron[tile] = 1;
              const float coeff = get_coeff_for_ron_DP(
                  tile, my_pid, cal_proto_sequence_value[loc_first][loc_second], candidate_pon_ron,
                  candidate_is_ron, my_tenpai_prob, deal_in_tile_prob, tenpai_prob_other, tn,
                  riichi_ron_ratio_para, not_riichi_ron_ratio_para, hidden_tenpai_ron_ratio);
              ron_candidates[tile].push_back(
                  sparsemax_bellman::Candidate({1.0, win.ron_exp, 0.0, win.ron_exp}, coeff));
            }
          }
          const std::array<int, 3> &open_loc =
              hand_calculator_work.get_const_open_meld_edge_loc(cn, gn);
          for (int acn = open_loc[1]; acn < open_loc[2]; ++acn) {
            const Hand_Action &edge =
                hand_calculator_work.cand_graph_sub_open_meld_work[open_loc[0]][acn];
            const int tile = edge.tile, tile_out = edge.tile_out;
            const int dst_group = edge.dst_group, dst_sub = edge.dst_group_sub;
            const Hand_Location &dst =
                hand_calculator_work.candidates_work[dst_group].proto_sequence_loc[dst_sub];
            const int df = dst.get_first(), ds = dst.get_second();
            const double survival = 1.0 - deal_in_p_tile[tn - 1][tile_out];
            int candidate_pon_ron[38] = {}, candidate_is_ron[38] = {};
            // Match the clean full-DP path: open kan does not get the pon
            // coefficient; only AT_PON sets pon_ron[tile].
            static_assert(AT_PON != AT_OPEN_KAN, "open kan must not share the pon coefficient");
            candidate_pon_ron[tile] = (edge.action_type == AT_PON);
            const float coeff = get_coeff_for_ron_DP(
                tile, my_pid, cal_proto_sequence_value[loc_first][loc_second], candidate_pon_ron,
                candidate_is_ron, my_tenpai_prob, deal_in_tile_prob, tenpai_prob_other, tn,
                riichi_ron_ratio_para, not_riichi_ron_ratio_para, hidden_tenpai_ron_ratio);
            ron_candidates[tile].push_back(sparsemax_bellman::Candidate(
                {survival * hand_calculator_work.win_prob_work[df][ds][mod2tn_prev],
                 survival * hand_calculator_work.win_exp_work[df][ds][mod2tn_prev],
                 survival * hand_calculator_work.tenpai_prob_work[df][ds][mod2tn_prev],
                 survival * hand_calculator_work.points_exp_work[df][ds][mod2tn_prev] +
                     deal_in_e_tile[tn - 1][tile_out]},
                coeff));
          }
          for (int tile = 1; tile < 38; ++tile) {
            if (tile % 10 == 0) continue;
            int remaining_tmp =
                4 - cal_proto_sequence_value[loc_first][loc_second].using_tile_kind_num(tile) -
                tile_visible[tile];
            if (using_tile_kind_array[tile] -
                    cal_proto_sequence_value[loc_first][loc_second].using_tile_kind_num(tile) >
                0) {
              remaining_tmp -=
                  using_tile_kind_array[tile] -
                  cal_proto_sequence_value[loc_first][loc_second].using_tile_kind_num(tile);
            }
            const double event_weight =
                double(remaining_tmp) / (remaining_tile_num - (tsumo_num - tn));
            for (std::size_t i = 1; i < ron_candidates[tile].size(); ++i)
              ron_candidates[tile][i].event_weight = event_weight;
            const sparsemax_bellman::Record relaxed =
                sparsemax_bellman::mix_delta(ron_candidates[tile], baseline);
            hand_calculator_work.win_prob_work[loc_first][loc_second][mod2tn] += relaxed.win_prob;
            hand_calculator_work.win_exp_work[loc_first][loc_second][mod2tn] += relaxed.win_exp;
            hand_calculator_work.tenpai_prob_work[loc_first][loc_second][mod2tn] +=
                relaxed.tenpai_prob;
            hand_calculator_work.points_exp_work[loc_first][loc_second][mod2tn] +=
                relaxed.points_exp;
          }
        }
        double other_end_prob_tmp, other_end_value_tmp;
        if (cal_proto_sequence_value[loc_first][loc_second].get_riichi() == 1) {
          other_end_prob_tmp = riichi_other_end_prob[tn - 1];
          other_end_value_tmp = exp_other_ar;
        } else if (cal_proto_sequence_value[loc_first][loc_second].get_kan_changed() == 1) {
          other_end_prob_tmp = other_end_prob[tn - 1];
          other_end_value_tmp = exp_other_kan;
        } else {
          other_end_prob_tmp = other_end_prob[tn - 1];
          other_end_value_tmp = exp_other;
        }

        hand_calculator_work.tenpai_prob_work[loc_first][loc_second][mod2tn] =
            (1.0 - other_end_prob_tmp) *
            hand_calculator_work.tenpai_prob_work[loc_first][loc_second][mod2tn];
        hand_calculator_work.win_prob_work[loc_first][loc_second][mod2tn] =
            (1.0 - other_end_prob_tmp) *
            hand_calculator_work.win_prob_work[loc_first][loc_second][mod2tn];
        hand_calculator_work.win_exp_work[loc_first][loc_second][mod2tn] =
            (1.0 - other_end_prob_tmp) *
            hand_calculator_work.win_exp_work[loc_first][loc_second][mod2tn];
        hand_calculator_work.points_exp_work[loc_first][loc_second][mod2tn] =
            other_end_prob_tmp * other_end_value_tmp +
            (1.0 - other_end_prob_tmp) *
                hand_calculator_work.points_exp_work[loc_first][loc_second][mod2tn];

        if (fold_choice_mode > 0 &&
            cal_proto_sequence_value[loc_first][loc_second].get_riichi() == 0) {
          if (tactics.fold_exp_at_dp_open_meld ||
              cal_proto_sequence_value[loc_first][loc_second].get_open_meld_num() ==
                  game_state.player_state[my_pid].open_meld.size()) {
            const sparsemax_bellman::Record play = {
                hand_calculator_work.win_prob_work[loc_first][loc_second][mod2tn],
                hand_calculator_work.win_exp_work[loc_first][loc_second][mod2tn],
                hand_calculator_work.tenpai_prob_work[loc_first][loc_second][mod2tn],
                hand_calculator_work.points_exp_work[loc_first][loc_second][mod2tn]};
            const sparsemax_bellman::Record fold = {
                0.0, 0.0, 0.0, hand_calculator_work.fold_exp_work[loc_first][loc_second]};
            std::vector<sparsemax_bellman::Candidate> choices;
            choices.push_back(sparsemax_bellman::Candidate(play));
            choices.push_back(sparsemax_bellman::Candidate(fold));
            sparsemax_bellman::assign(
                sparsemax_bellman::mix_all(choices),
                hand_calculator_work.win_prob_work[loc_first][loc_second][mod2tn],
                hand_calculator_work.win_exp_work[loc_first][loc_second][mod2tn],
                hand_calculator_work.tenpai_prob_work[loc_first][loc_second][mod2tn],
                hand_calculator_work.points_exp_work[loc_first][loc_second][mod2tn]);
          }
        }
      }
    }
  }
}
