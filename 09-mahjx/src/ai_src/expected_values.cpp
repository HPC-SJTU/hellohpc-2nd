#include "expected_values.hpp"

#include <cstdlib>

#include "params.hpp"

extern const bool console_out;
extern std::array<Tactics, 4> tactics_all;

float get_drawn_round_pattern_prob(const std::array<int, 4> &flag,
                                   const std::array<float, 4> &formal_tenpai_prob) {
  float res = 1.0;
  for (int pid = 0; pid < 4; pid++) {
    if (flag[pid] == 1) {
      res *= formal_tenpai_prob[pid];
    } else {
      res *= 1.0 - formal_tenpai_prob[pid];
    }
  }
  return res;
}

int cal_tsumo_num_exp(const int my_pid, const Game_State &game_state, const int discard_inc,
                      const std::array<float, 4> tenpai_prob) {
  // discard_inc can be part of the model, but the code does not use it now.
  {
    const int acn = std::min(std::max(1, (int)game_state.player_state[my_pid].river.size()), 16);
    float w[4];
    float x[4] = {};
    int rp = 0;
    const float *const params = TSUMO_COUNT_PARA[acn];
    for (int i = 0; i < 4; i++) w[i] = params[i];

    float tp = 1.0;
    for (int pid = 0; pid < 4; pid++) {
      if (pid != my_pid) {
        if (game_state.player_state[pid].riichi_declared) {
          rp++;
        } else {
          tp = tp * (1.0 - infer_tenpai_prob_old(game_state.player_state[pid].river,
                                                 game_state.player_state[pid].open_meld.size()));
        }
      }
    }
    tp = 1.0 - tp;

    x[0] = 1.0;
    if (rp >= 2) {
      x[2] = 1.0;
    } else if (rp == 1) {
      x[1] = 1.0;
    }
    x[3] = tp;
    return ceil(logistic(w, x, 4) * (18 - acn));
  }
}

float cal_my_win_prob(const int my_pid, const Moves &game_record, const float win_prob_sol,
                      const Game_State &game_state, const int discard_inc,
                      const std::array<float, 4> &tenpai_prob) {
  if (game_record.back().type == EventType::DRAW &&
      get_other_riichi_declared_num(my_pid, game_state) == 0 &&
      game_state.player_state[my_pid].open_meld.size() == 0 &&
      get_max_other_open_meld_num(my_pid, game_state) <= tactics_all[my_pid].win_coeff_tp_fnm &&
      game_state.player_state[my_pid].river.size() <= tactics_all[my_pid].win_coeff_tp_an) {
    return win_prob_sol;
  }

  {
    // TODO: Document and reconcile the light-model difference from the reference implementation.
    const int act_num = (int)game_state.player_state[my_pid].river.size() + discard_inc;
    const float *const params = WIN_PARA[std::max(1, std::min(act_num, 15))];
    float w[4];
    for (int i = 0; i < 4; i++) w[i] = params[i];

    float x[4];
    x[0] = 1.0;
    x[1] = get_other_riichi_declared_num(my_pid, game_state);
    float tmp = 1.0;
    for (int pid = 0; pid < 4; pid++) {
      if (pid != my_pid && !game_state.player_state[pid].riichi_declared) {
        tmp *= 1.0 - tenpai_prob[pid];
      }
    }
    x[2] = 1.0 - tmp;
    x[3] = my_logit(win_prob_sol);
    return logistic(w, x, 4);
  }
}

float cal_my_win_value(const double win_prob_sol, const double points_gain,
                       const double value_not_win) {
  if (win_prob_sol == 0.0) {
    return 0.0;
  } else {
    return value_not_win + points_gain / win_prob_sol;
  }
}

float cal_drawn_round_prob(const int my_pid, const Game_State &game_state, const float my_win_prob,
                           const std::array<float, 4> &tenpai_prob, const int discard_inc) {
  {
    const int act_num = game_state.player_state[my_pid].river.size() + discard_inc;
    const float *params = nullptr;
    if (act_num <= 6) {
      if (game_state.player_state[my_pid].riichi_declared) {
        params = RIICHI_DRAWN_ROUND_PARA1_6;
      } else {
        params = DRAWN_ROUND_PARA1_6;
      }
    } else {
      if (game_state.player_state[my_pid].riichi_declared) {
        params = RIICHI_DRAWN_ROUND_PARA[std::min(act_num, 18)];
      } else {
        params = DRAWN_ROUND_PARA[std::min(act_num, 18)];
      }
    }
    float w[4];
    for (int i = 0; i < 4; i++) w[i] = params[i];
    float x[4];
    x[0] = 1.0;
    const int riichi_player_num_other = get_other_riichi_declared_num(my_pid, game_state);
    if (act_num <= 6) {
      x[1] = act_num;
      x[2] = (riichi_player_num_other > 0) ? 1.0 : 0.0;
    } else {
      x[1] = 0.0;
      x[2] = 0.0;
      if (riichi_player_num_other > 1) {
        x[1] = 1.0;
      } else if (riichi_player_num_other > 0) {
        x[2] = 1.0;
      }
    }
    float tmp = 1.0;
    for (int pid = 0; pid < 4; pid++) {
      if (pid != my_pid && !game_state.player_state[pid].riichi_declared) {
        tmp *= 1.0 - tenpai_prob[pid];
      }
    }
    x[3] = 1.0 - tmp;
    return logistic(w, x, 4);
  }
}

float cal_my_formal_tenpai_prob(const int my_pid, const Game_State &game_state,
                                const int discard_inc, const float formal_tenpai_prob_sol) {
  if (game_state.player_state[my_pid].riichi_declared) {
    return 1.0;
  } else {
    {
      const int act_num = (int)game_state.player_state[my_pid].river.size() + discard_inc;
      const float *const params = MY_FORMAL_TENPAI_PARA[std::min(act_num, 17)];
      float w[3];
      for (int i = 0; i < 3; i++) w[i] = params[i];

      float x[3];
      x[0] = 1.0;
      x[1] = formal_tenpai_prob_sol;
      x[2] = std::min(1, get_other_riichi_declared_num(my_pid, game_state));
      return logistic(w, x, 3);
    }
  }
}

float cal_other_formal_tenpai_prob(const int my_pid, const int target_pid,
                                   const Game_State &game_state, const int discard_inc,
                                   const float current_tenpai_prob) {
  if (game_state.player_state[target_pid].riichi_declared) {
    return 1.0;
  } else {
    {
      const int act_num = (int)game_state.player_state[my_pid].river.size() + discard_inc;
      const float *const params = OTHER_FORMAL_TENPAI_PARA[std::min(act_num, 17)];
      float w[2], x[2];
      for (int i = 0; i < 2; i++) w[i] = params[i];

      x[0] = 1.0;
      x[1] = current_tenpai_prob;
      return logistic(w, x, 2);
    }
  }
}

std::array<float, 4> cal_formal_tenpai_prob(const int my_pid, const Game_State &game_state,
                                            const int discard_inc,
                                            const float formal_tenpai_prob_sol,
                                            const std::array<float, 4> &tenpai_prob) {
  std::array<float, 4> formal_tenpai_prob;
  for (int pid = 0; pid < 4; pid++) {
    if (pid == my_pid) {
      formal_tenpai_prob[pid] =
          cal_my_formal_tenpai_prob(my_pid, game_state, discard_inc, formal_tenpai_prob_sol);
    } else {
      formal_tenpai_prob[pid] =
          cal_other_formal_tenpai_prob(my_pid, pid, game_state, discard_inc, tenpai_prob[pid]);
    }
  }
  return formal_tenpai_prob;
}

float cal_drawn_round_value(
    const std::array<float, 4> &formal_tenpai_prob,
    const std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> &drawn_round_pt_exp) {
  float drawn_round_value = 0.0;
  std::array<int, 4> flag_tmp;
  for (int t0 = 0; t0 < 2; t0++) {
    for (int t1 = 0; t1 < 2; t1++) {
      for (int t2 = 0; t2 < 2; t2++) {
        for (int t3 = 0; t3 < 2; t3++) {
          flag_tmp[0] = t0;
          flag_tmp[1] = t1;
          flag_tmp[2] = t2;
          flag_tmp[3] = t3;
          drawn_round_value += drawn_round_pt_exp[t0][t1][t2][t3] *
                               get_drawn_round_pattern_prob(flag_tmp, formal_tenpai_prob);
        }
      }
    }
  }
  return drawn_round_value;
}

float cal_not_ready_drawn_round_value(
    const int my_pid, const std::array<float, 4> &formal_tenpai_prob,
    const std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> &drawn_round_pt_exp) {
  std::array<float, 4> formal_tenpai_prob_new = formal_tenpai_prob;
  formal_tenpai_prob_new[my_pid] = 0.0;
  return cal_drawn_round_value(formal_tenpai_prob_new, drawn_round_pt_exp);
}

std::array<std::array<float, 4>, 4> cal_target_prob(const int my_pid,
                                                    const std::array<float, 4> &risk_array,
                                                    const Game_State &game_state,
                                                    const int discard_inc,
                                                    const int open_meld_num_inc) {
  std::array<std::array<float, 4>, 4> target_prob;
  {
    float w[3], x[3];
    x[0] = 1.0;
    const int act_num = (int)game_state.player_state[my_pid].river.size() + discard_inc;
    const float *const params = DEAL_IN_PARA[std::max(6, std::min(act_num, 14))];
    for (int i = 0; i < 3; i++) w[i] = params[i];

    for (int pid1 = 0; pid1 < 4; pid1++) {
      if (pid1 != my_pid) {
        x[1] = risk_array[pid1];
        x[2] = (float)(game_state.player_state[my_pid].open_meld.size() + open_meld_num_inc);
        if (act_num <= 4) {
          target_prob[pid1][my_pid] = 2.0 / 11.0;
        } else {
          target_prob[pid1][my_pid] = logistic(w, x, 2);
        }
        for (int pid2 = 0; pid2 < 4; pid2++) {
          if (pid2 != my_pid) {
            if (pid1 == pid2) {
              target_prob[pid1][pid2] = (1.0 - target_prob[pid1][my_pid]) * 5.0 / 9.0;
            } else {
              target_prob[pid1][pid2] = (1.0 - target_prob[pid1][my_pid]) * 2.0 / 9.0;
            }
          }
        }
      }
    }
    for (int pid = 0; pid < 4; pid++) {
      if (pid == my_pid) {
        target_prob[my_pid][pid] = 5.0 / 11.0;
      } else {
        target_prob[my_pid][pid] = 2.0 / 11.0;
      }
    }
  }
  return target_prob;
}

std::array<std::array<float, 4>, 4> cal_target_prob_other(const int my_pid) {
  std::array<std::array<float, 4>, 4> target_prob;
  for (int pid1 = 0; pid1 < 4; pid1++) {
    for (int pid2 = 0; pid2 < 4; pid2++) {
      if (pid1 == my_pid || pid2 == my_pid) {
        target_prob[pid1][pid2] = 0.0;
      } else if (pid1 == pid2) {
        target_prob[pid1][pid2] = 5.0 / 9.0;
      } else {
        target_prob[pid1][pid2] = 2.0 / 9.0;
      }
    }
  }
  return target_prob;
}

double cal_target_value_child(const std::array<std::array<float, 12>, 14> &round_end_pt_exp,
                              const std::array<std::array<float, 12>, 14> &han_prob) {
  double value = 0.0;
  for (int han = 0; han < 14; han++) {
    for (int fu = 0; fu < 12; fu++) {
      value += double(round_end_pt_exp[han][fu]) * double(han_prob[han][fu]);
    }
  }
  return value;
}

std::array<std::array<double, 4>, 4> cal_target_value(
    const int my_pid,
    const std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> &round_end_pt_exp,
    const float my_win_value,
    const std::array<std::array<std::array<float, 12>, 14>, 4> &tsumo_prob,
    const std::array<std::array<std::array<float, 12>, 14>, 4> &ron_prob) {
  std::array<std::array<double, 4>, 4> target_value;
  for (int pid1 = 0; pid1 < 4; pid1++) {
    for (int pid2 = 0; pid2 < 4; pid2++) {
      if (pid1 == my_pid) {
        target_value[pid1][pid2] = my_win_value;
      } else if (pid1 == pid2) {
        target_value[pid1][pid2] =
            cal_target_value_child(round_end_pt_exp[pid1][pid2], tsumo_prob[pid1]);
      } else {
        target_value[pid1][pid2] =
            cal_target_value_child(round_end_pt_exp[pid1][pid2], ron_prob[pid1]);
      }
    }
  }
  return target_value;
}

std::array<float, 5> cal_round_result_prob(const int my_pid, const Game_State &game_state,
                                           const int discard_inc, const float my_win_prob,
                                           const std::array<float, 4> &tenpai_prob,
                                           const float drawn_round_prob) {
  std::array<float, 5> round_result_prob;
  round_result_prob[my_pid] = my_win_prob;
  round_result_prob[4] = (1.0 - my_win_prob) * drawn_round_prob;

  {
    const int act_num = game_state.player_state[my_pid].river.size() + discard_inc;
    float w[21];
    const float *const params =
        (act_num <= 7) ? OTHER_RESULT_PARA1_6 : OTHER_RESULT_PARA[std::min(act_num, 17)];
    for (int i = 0; i < 21; i++) w[i] = params[i];
    float x[7];
    x[0] = 1.0;
    for (int pid_add = 1; pid_add <= 3; pid_add++) {
      const int pid = (my_pid + pid_add) % 4;
      if (game_state.player_state[pid].riichi_declared) {
        x[pid_add] = 1.0;
        x[pid_add + 3] = 0.0;
      } else {
        x[pid_add] = 0.0;
        x[pid_add + 3] = tenpai_prob[pid];
      }
    }
    float tmp_array[3];
    MC_logistic(w, x, tmp_array, 7, 3);
    for (int pid_add = 1; pid_add <= 3; pid_add++) {
      const int pid = (my_pid + pid_add) % 4;
      round_result_prob[pid] =
          (1.0 - my_win_prob) * (1.0 - drawn_round_prob) * tmp_array[pid_add - 1];
    }
  }
  return round_result_prob;
}

std::array<double, 5> cal_round_result_value(
    const std::array<std::array<float, 4>, 4> &target_prob,
    const std::array<std::array<double, 4>, 4> &target_value, const float drawn_round_value) {
  std::array<double, 5> round_result_value;
  for (int pid1 = 0; pid1 < 4; pid1++) {
    round_result_value[pid1] = 0.0;
    for (int pid2 = 0; pid2 < 4; pid2++) {
      round_result_value[pid1] += double(target_prob[pid1][pid2]) * target_value[pid1][pid2];
    }
  }
  round_result_value[4] = drawn_round_value;
  return round_result_value;
}

std::array<float, 4> cal_risk_array(const int my_pid, const Game_State &game_state,
                                    const int discard_inc, const Tile_Array &hand,
                                    const std::array<float, 4> tenpai_prob,
                                    const std::array<std::array<float, 38>, 4> &deal_in_tile_prob) {
  std::array<float, 4> risk_array;
  const int tsumo_num_exp = cal_tsumo_num_exp(my_pid, game_state, discard_inc, tenpai_prob);
  std::array<float, 38> deal_in_tile_value_tmp;
  for (int tile = 0; tile < 38; tile++) {
    deal_in_tile_value_tmp[tile] = -1.0;
  }
  const Tile_Array &hand_kind = tile_kind(hand);
  for (int pid = 0; pid < 4; pid++) {
    Full_Defense full_defense = cal_full_defense(hand_kind, deal_in_tile_prob[pid],
                                                 deal_in_tile_value_tmp, 0.0, 0.0, tsumo_num_exp);
    risk_array[pid] = full_defense.full_defense_deal_in_prob;
  }
  return risk_array;
}

float cal_exp(
    const int my_pid, const Moves &game_record, const Game_State &game_state,
    const Tile_Array &result_hand, const double win_prob_sol, const double points_gain,
    const double value_not_win, const float formal_tenpai_prob_sol,
    const std::array<float, 4> &tenpai_prob,
    const std::array<std::array<float, 38>, 4> &deal_in_tile_prob,
    const std::array<std::array<std::array<float, 12>, 14>, 4> &tsumo_prob,
    const std::array<std::array<std::array<float, 12>, 14>, 4> &ron_prob,
    const std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> &round_end_pt_exp,
    const std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> &drawn_round_pt_exp,
    const int discard_inc, const int open_meld_inc) {
  const float my_win_prob =
      cal_my_win_prob(my_pid, game_record, win_prob_sol, game_state, discard_inc, tenpai_prob);
  const float my_win_value = cal_my_win_value(win_prob_sol, points_gain, value_not_win);

  const float drawn_round_prob =
      cal_drawn_round_prob(my_pid, game_state, my_win_prob, tenpai_prob, discard_inc);
  // TODO: Include tenpai_prob in the formal-tenpai probability.
  const std::array<float, 4> formal_tenpai_prob =
      cal_formal_tenpai_prob(my_pid, game_state, discard_inc, formal_tenpai_prob_sol, tenpai_prob);
  const float drawn_round_value = cal_drawn_round_value(formal_tenpai_prob, drawn_round_pt_exp);

  const std::array<float, 5> round_result_prob = cal_round_result_prob(
      my_pid, game_state, discard_inc, my_win_prob, tenpai_prob, drawn_round_prob);
  const std::array<float, 4> risk_array =
      cal_risk_array(my_pid, game_state, discard_inc, result_hand, tenpai_prob, deal_in_tile_prob);
  const std::array<std::array<float, 4>, 4> target_prob =
      cal_target_prob(my_pid, risk_array, game_state, discard_inc, open_meld_inc);
  const std::array<std::array<double, 4>, 4> target_value =
      cal_target_value(my_pid, round_end_pt_exp, my_win_value, tsumo_prob, ron_prob);
  const std::array<double, 5> round_result_value =
      cal_round_result_value(target_prob, target_value, drawn_round_value);

  float exp_value = 0.0;
  for (int i = 0; i < 5; i++) {
    exp_value += round_result_prob[i] * round_result_value[i];
    if (console_out) {
      std::cout << "round_result:" << round_result_prob[i] << " " << round_result_value[i]
                << std::endl;
    }
  }
  if (console_out) {
    std::cout << "expected_value:" << win_prob_sol << " " << formal_tenpai_prob_sol << " "
              << points_gain << " " << value_not_win << " " << my_win_prob << " " << my_win_value
              << " " << round_result_value[4] << " " << exp_value << std::endl;
  }
  return exp_value;
}

float cal_passive_drawn_round_value(
    const int my_pid, const Game_State &game_state, const std::array<float, 4> &tenpai_prob,
    const std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> &drawn_round_pt_exp) {
  // TODO: Include tenpai_prob in the formal-tenpai probability.
  std::array<float, 4> formal_tenpai_prob = cal_formal_tenpai_prob(
      my_pid, game_state, 0, game_state.player_state[my_pid].riichi_declared ? 1.0 : 0.0,
      tenpai_prob);
  formal_tenpai_prob[my_pid] = 0.0;
  return cal_drawn_round_value(formal_tenpai_prob, drawn_round_pt_exp);
}

double cal_other_end_value(
    const int my_pid, const Game_State &game_state, const std::array<float, 4> &tenpai_prob,
    const std::array<std::array<std::array<float, 12>, 14>, 4> &tsumo_prob,
    const std::array<std::array<std::array<float, 12>, 14>, 4> &ron_prob,
    const std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> &round_end_pt_exp) {
  const std::array<float, 5> round_result_prob =
      cal_round_result_prob(my_pid, game_state, 0, 0.0, tenpai_prob, 0.0);
  const std::array<std::array<float, 4>, 4> target_prob = cal_target_prob_other(my_pid);
  const std::array<std::array<double, 4>, 4> target_value =
      cal_target_value(my_pid, round_end_pt_exp, 0.0, tsumo_prob, ron_prob);
  const std::array<double, 5> round_result_value =
      cal_round_result_value(target_prob, target_value, 0.0);

  double exp_value = 0.0;
  for (int i = 0; i < 5; i++) {
    exp_value += double(round_result_prob[i]) * double(round_result_value[i]);
  }
  return exp_value;
}

float cal_full_defense_exp(const int my_pid, const Game_State &game_state,
                           const Tile_Array &hand_tmp,
                           const std::array<float, 38> &full_defense_deal_in_tile_prob,
                           const std::array<float, 38> &total_deal_in_tile_value,
                           const float not_win_value, const float other_end_value,
                           const float passive_drawn_round_value,
                           const float passive_drawn_round_prob, const int tsumo_num_exp,
                           const Tactics &tactics) {
  // We set the default full-defense value to not_win_value. If we use other_end_value,
  // the code can ignore the win probability result when other_end_value is much larger than
  // not_win_value. Whether this choice is stronger is unverified. We need the probability of no
  // deal-in, including the drawn round, when we fold. The inclusive_fold model seems correct.
  {
    // TODO: Compare the full-defense input with the reference implementation.
    Full_Defense full_defense =
        cal_full_defense(hand_tmp, full_defense_deal_in_tile_prob, total_deal_in_tile_value,
                         other_end_value, passive_drawn_round_value, tsumo_num_exp);
    float w[3];
    const int act_num = std::min(
        std::max(2, (int)game_state.player_state[my_pid].river.size()),
        18);  // The reference implementation does not use river size + 1, so we do the same.
    const float *const params = FULL_DEFENSE_PARA[act_num];
    for (int i = 0; i < 3; i++) w[i] = params[i];
    full_defense.modify_full_defense_value_with_drawn_round(
        game_state.player_state[my_pid].open_meld.size(), w, passive_drawn_round_prob);
    return full_defense.full_defense_exp;
  }
}
