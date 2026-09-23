#include "selector.hpp"

Hand_Calculator_Work hand_calculator_work;
bool console_out;

std::array<Tactics, 4> tactics_all;

Tile_Choice::Tile_Choice()
    : action_type(AT_NULL), tile(0), pt_exp_after(0.0), pt_exp_total(0.0), pt_exp_after_fold(0.0) {}

bool Tile_Choice::operator<(const Tile_Choice &rhs) const {
  if (pt_exp_total < rhs.pt_exp_total) {
    return true;
  } else {
    return false;
  }
}

Moves Tile_Choice::get_moves(const Game_State &game_state, const int my_pid,
                             const int tsumo_tile) const {
  Moves moves;
  if (action_type == AT_TSUMO_WIN) {
    moves.push_back(make_win(my_pid, my_pid, tsumo_tile));
    return moves;
  }
  if (action_type == AT_NINE_TERMINALS) {
    moves.push_back(make_nine_terminals(my_pid));
    return moves;
  }
  if (action_type == AT_CONCEALED_KAN) {
    if (tile_kind(tile) < 30 && tile_kind(tile) % 10 == 5) {
      moves.push_back(make_concealed_kan_red(my_pid, tile));
    } else {
      moves.push_back(make_concealed_kan_default(my_pid, tile));
    }
    return moves;
  }
  if (action_type == AT_UPGRADED_KAN) {
    if (tile < 30 && tile % 10 == 5) {
      moves.push_back(make_upgraded_kan_red(my_pid, tile));
    } else {
      moves.push_back(make_upgraded_kan_default(my_pid, tile));
    }
    return moves;
  }

  if (action_type == AT_RIICHI_DECLARE) {
    if (!game_state.player_state[my_pid].riichi_declared) {
      moves.push_back(make_riichi(my_pid));
    }
  }
  if (action_type == AT_DISCARD || AT_RIICHI_DECLARE) {
    moves.push_back(make_discard(my_pid, tile, tile == tsumo_tile));
  }
  return moves;
}

void Open_Meld_Choice::reset() {
  open_meld_tile = 0;
  open_meld_action_type = AT_NULL;
  tile_out = 0;

  for (int i = 0; i < 3; i++) {
    exposed_tile[i] = 0;
  }
  pt_exp_total = 0.0;
  pt_exp_total_prev = 0.0;
  pt_exp_after = 0.0;
  pt_exp_after_prev = 0.0;
}

Open_Meld_Choice::Open_Meld_Choice() { reset(); }

bool Open_Meld_Choice::operator<(const Open_Meld_Choice &rhs) const {
  if (pt_exp_total < rhs.pt_exp_total) {
    return true;
  } else {
    return false;
  }
}

Moves Open_Meld_Choice::get_moves(const int my_pid, const int target) const {
  Moves moves;
  if (open_meld_action_type == AT_RON_WIN) {
    moves.push_back(make_win(my_pid, target, open_meld_tile));
  } else if (open_meld_action_type == AT_OPEN_MELD_PASS) {
    moves.push_back(make_none(my_pid));
  } else if (open_meld_action_type == AT_OPEN_KAN) {
    moves.push_back(make_open_kan(my_pid, target, open_meld_tile,
                                  {exposed_tile[0], exposed_tile[1], exposed_tile[2]}));
  } else if (is_pon(open_meld_action_type)) {
    moves.push_back(make_pon(my_pid, target, open_meld_tile, {exposed_tile[0], exposed_tile[1]}));
    moves.push_back(make_discard(my_pid, tile_out, false));
  } else if (is_chii(open_meld_action_type)) {
    moves.push_back(make_chii(my_pid, target, open_meld_tile, {exposed_tile[0], exposed_tile[1]}));
    moves.push_back(make_discard(my_pid, tile_out, false));
  }
  return moves;
}

std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> cal_round_end_pt_exp(
    const Moves &game_record, const Game_State &game_state, const int my_pid,
    const bool riichi_mode, const Tactics &tactics) {
  int riichi_stick = game_state.deposit;
  for (int pid = 0; pid < 4; pid++) {
    if ((game_state.player_state[pid].riichi_declared || (pid == my_pid && riichi_mode)) &&
        !game_state.player_state[pid].riichi_accepted) {
      riichi_stick++;
    }
    // Use riichi_declared for the riichi state. This makes the open meld decision for the declared
    // tile correct. For a ron on the declared tile, we calculate the expected value separately.
  }
  const int ranking_model_round = game_state.ranking_model_round;
  const int dealer_id = get_dealer(game_record);

  std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> round_end_pt_exp = {};
#pragma omp parallel
#pragma omp for
  for (int pid1 = 0; pid1 < 4; pid1++) {
    for (int pid2 = 0; pid2 < 4; pid2++) {
      for (int han = 1; han <= 13; han++) {
        for (int fu = 1; fu <= 11; fu++) {
          if (fu > 2 && han >= 5) {
            round_end_pt_exp[pid1][pid2][han][fu] = round_end_pt_exp[pid1][pid2][han][2];
            // Mini-points do not change a mangan-or-higher score.
          } else {
            round_end_pt_exp[pid1][pid2][han][fu] = 0;
            int fu_mod = 0;
            if (fu == 1 && pid1 != pid2) {
              continue;
            }  // No pinfu tsumo from another player.
            if (fu == 1 && pid1 == pid2 && han == 1) {
              continue;
            }  // A pinfu tsumo has no 1 han.
            if (fu == 2 && pid1 == pid2 && han < 3) {
              continue;
            }  // A seven pairs tsumo has 3 han or more.
            if (fu == 2 && pid1 != pid2 && han == 1) {
              continue;
            }  // A seven pairs ron has 2 han or more.

            if (fu == 1) {
              fu_mod = 20;
            } else if (fu == 2) {
              fu_mod = 25;
            } else {
              fu_mod = fu * 10;
            }

            const std::array<int, 4> points_move = points_move_win(
                pid1, pid2, han, fu_mod, dealer_id, game_state.repeat_counter, riichi_stick);
            std::array<int, 4> points_tmp;
            for (int pid = 0; pid < 4; pid++) {
              points_tmp[pid] = game_state.player_state[pid].score + points_move[pid];
              if ((game_state.player_state[pid].riichi_declared ||
                   (pid == my_pid && riichi_mode)) &&
                  !game_state.player_state[pid].riichi_accepted) {
                points_tmp[pid] -= 1000;
              }
            }

            assert(dealer_id - ranking_model_round + 12 >= 0);
            const int next_ranking_model_round =
                (pid1 == dealer_id) ? ranking_model_round : ranking_model_round + 1;
            const int dealer_id_next = (pid1 == dealer_id) ? dealer_id : (dealer_id + 1) % 4;
            const std::array<std::array<float, 4>, 4> turn_prob = calc_turn_prob(
                next_ranking_model_round, points_tmp, dealer_id_next, pid1 == dealer_id, tactics);
            for (int j = 0; j < 4; j++) {
              round_end_pt_exp[pid1][pid2][han][fu] += turn_prob[my_pid][j] * tactics.turn_pt[j];
            }
          }
        }
      }
    }
  }
  return round_end_pt_exp;
}

std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> cal_drawn_round_pt_exp(
    const Moves &game_record, const Game_State &game_state, const int my_pid,
    const bool riichi_mode, const Tactics &tactics) {
  int riichi_stick = game_state.deposit;
  for (int pid = 0; pid < 4; pid++) {
    if (game_state.player_state[pid].riichi_declared &&
        !game_state.player_state[pid].riichi_accepted) {
      riichi_stick++;
    }
    // Use riichi_declared for the riichi state. This makes the open meld decision for the declared
    // tile correct. For a ron on the declared tile, we calculate the expected value separately.
  }
  const int ranking_model_round = game_state.ranking_model_round;
  const int dealer_id = get_dealer(game_record);

  std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> drawn_round_pt_exp = {};

  std::array<bool, 4> is_tenpai;
  for (int t0 = 0; t0 < 2; t0++) {
    for (int t1 = 0; t1 < 2; t1++) {
      for (int t2 = 0; t2 < 2; t2++) {
        for (int t3 = 0; t3 < 2; t3++) {
          is_tenpai[0] = (t0 == 1);
          is_tenpai[1] = (t1 == 1);
          is_tenpai[2] = (t2 == 1);
          is_tenpai[3] = (t3 == 1);
          const std::array<int, 4> points_move = points_move_drawn_round(is_tenpai);

          std::array<int, 4> points_tmp;
          for (int pid = 0; pid < 4; pid++) {
            points_tmp[pid] = game_state.player_state[pid].score + points_move[pid];
            if ((game_state.player_state[pid].riichi_declared || (pid == my_pid && riichi_mode)) &&
                !game_state.player_state[pid].riichi_accepted) {
              points_tmp[pid] -= 1000;
            }
          }

          assert(dealer_id - ranking_model_round + 12 >= 0);
          const int next_ranking_model_round =
              is_tenpai[dealer_id] ? ranking_model_round : ranking_model_round + 1;
          const int dealer_id_next = is_tenpai[dealer_id] ? dealer_id : (dealer_id + 1) % 4;
          const std::array<std::array<float, 4>, 4> turn_prob = calc_turn_prob(
              next_ranking_model_round, points_tmp, dealer_id_next, is_tenpai[dealer_id], tactics);
          for (int j = 0; j < 4; j++) {
            drawn_round_pt_exp[t0][t1][t2][t3] += turn_prob[my_pid][j] * tactics.turn_pt[j];
          }
        }
      }
    }
  }
  return drawn_round_pt_exp;
}

int cal_tsumo_num_DP(const Moves &game_record, const int my_pid) {
  const int tsumo_num_all = count_tsumo_num_all(game_record);
  const int result_tmp = (70 - tsumo_num_all) / 4;
  const Event &last_action = game_record[game_record.size() - 1];

  const int next_tsumo_player = last_action.type == EventType::DISCARD
                                    ? next_player(last_action.player, 1)
                                    : next_player(my_pid, 1);
  if ((70 - tsumo_num_all) % 4 > (4 + my_pid - next_tsumo_player) % 4) {
    return result_tmp + 1;
  } else {
    return result_tmp;
  }
}

void set_exposed_tile(const Hand_State2 &dst_hand_state, const Open_Meld_Vector &present_open_meld,
                      const int current_tile, int exposed_tile[3]) {
  Hand_State2 present_hand_state;
  present_hand_state.set_open_meld(present_open_meld);
  int open_meld_tmp[4][6];
  dst_hand_state.get_open_meld(open_meld_tmp, present_hand_state);
  bool open_meld_cand_passed = false;
  int exposed_counter = 0;
  for (int i = 0; i < 4; i++) {
    if (open_meld_tmp[present_open_meld.size()][2 + i] == 0) {
      break;
    }
    if (open_meld_tmp[present_open_meld.size()][2 + i] == current_tile &&
        open_meld_cand_passed == false) {
      open_meld_cand_passed = true;
    } else {
      exposed_tile[exposed_counter] = open_meld_tmp[present_open_meld.size()][2 + i];
      exposed_counter++;
    }
  }
}

int isolated_tile_most_needless(const Tile_Array &hand, const Tile_Array &visible,
                                const int round_wind, const int self_wind,
                                const std::vector<int> &dora_marker) {
  for (int h = 31; h < 38; h++) {
    if (hand[h] == 1 && visible[h] == 3) {
      return h;
    }
  }
  for (int h = 31; h < 38; h++) {
    if (hand[h] == 1 && visible[h] == 2) {
      return h;
    }
  }
  const std::vector<int> dora_vector = dora_marker_to_dora(dora_marker);
  for (int h = 31; h < 35; h++) {
    if (hand[h] == 1 && visible[h] == 1 && h != 31 + round_wind && h != 31 + self_wind &&
        std::count(dora_vector.begin(), dora_vector.end(), h) == 0) {
      return h;
    }
  }
  for (int h = 31; h < 35; h++) {
    if (hand[h] == 1 && visible[h] == 0 && h != 31 + round_wind && h != 31 + self_wind &&
        std::count(dora_vector.begin(), dora_vector.end(), h) == 0) {
      return h;
    }
  }
  for (int h = 31; h < 38; h++) {
    if (hand[h] == 1 && visible[h] == 1 && h != 31 + self_wind &&
        std::count(dora_vector.begin(), dora_vector.end(), h) == 0) {
      return h;
    }
  }
  for (int h = 31; h < 38; h++) {
    if (hand[h] == 1 && visible[h] == 1 &&
        std::count(dora_vector.begin(), dora_vector.end(), h) == 0) {
      return h;
    }
  }
  for (int c = 0; c < 3; c++) {
    if (hand[c * 10 + 1] == 1 && hand[c * 10 + 2] == 0 && hand[c * 10 + 3] == 0 &&
        hand[c * 10 + 4] == 1 &&
        std::count(dora_vector.begin(), dora_vector.end(), c * 10 + 1) == 0) {
      return c * 10 + 1;
    }
    if (hand[c * 10 + 9] == 1 && hand[c * 10 + 8] == 0 && hand[c * 10 + 7] == 0 &&
        hand[c * 10 + 6] == 1 &&
        std::count(dora_vector.begin(), dora_vector.end(), c * 10 + 9) == 0) {
      return c * 10 + 9;
    }
  }
  for (int c = 0; c < 3; c++) {
    if (hand[c * 10 + 1] == 1 && hand[c * 10 + 2] == 0 && hand[c * 10 + 3] == 0 &&
        std::count(dora_vector.begin(), dora_vector.end(), c * 10 + 1) == 0) {
      return c * 10 + 1;
    }
    if (hand[c * 10 + 9] == 1 && hand[c * 10 + 8] == 0 && hand[c * 10 + 7] == 0 &&
        std::count(dora_vector.begin(), dora_vector.end(), c * 10 + 9) == 0) {
      return c * 10 + 9;
    }
  }
  for (int h = 31; h < 38; h++) {
    if (hand[h] == 1 && visible[h] == 0 && h != 31 + self_wind &&
        std::count(dora_vector.begin(), dora_vector.end(), h) == 0) {
      return h;
    }
  }
  for (int h = 31; h < 38; h++) {
    if (hand[h] == 1 && visible[h] == 0 &&
        std::count(dora_vector.begin(), dora_vector.end(), h) == 0) {
      return h;
    }
  }
  for (int c = 0; c < 3; c++) {
    if (hand[c * 10 + 1] == 0 && hand[c * 10 + 2] == 1 && hand[c * 10 + 3] == 0 &&
        hand[c * 10 + 4] == 0 &&
        std::count(dora_vector.begin(), dora_vector.end(), c * 10 + 2) == 0) {
      return c * 10 + 2;
    }
    if (hand[c * 10 + 9] == 0 && hand[c * 10 + 8] == 1 && hand[c * 10 + 7] == 0 &&
        hand[c * 10 + 6] == 0 &&
        std::count(dora_vector.begin(), dora_vector.end(), c * 10 + 8) == 0) {
      return c * 10 + 8;
    }
  }
  for (int tile = 37; 0 < tile; tile--) {
    if (hand[tile] > 0) {
      return tile;
    }
  }
  return 0;
}

Selector::Selector() {
  tile_choice.clear();
  open_meld_choice.clear();
}

void Selector::set_selector(const Moves &game_record, const int my_pid, const Tactics &tactics) {
  tile_choice.clear();
  open_meld_choice.clear();
  const Game_State game_state = get_game_state(game_record);
  const Tile_Array &current_hand = game_state.player_state[my_pid].hand;
  const Tile_Array &current_hand_kind = tile_kind(current_hand);
  const Open_Meld_Vector &current_open_meld = game_state.player_state[my_pid].open_meld;
  const bool current_riichi = game_state.player_state[my_pid].riichi_declared;
  const Event &current_action = game_record[game_record.size() - 1];
  assert_with_out(
      current_action.type == EventType::DRAW ||
          (current_action.type == EventType::DISCARD && current_action.player != my_pid) ||
          (current_action.type == EventType::UPGRADED_KAN && current_action.player != my_pid),
      "set_selector current_action_type error");
  const int current_tile = current_action.tile;
  const Tile_Array tile_visible_all = get_tile_visible_all(game_state);
  const Tile_Array tile_visible_all_kind = tile_kind(tile_visible_all);
  const Tile_Array tile_visible_kind = sum_tile_array(tile_visible_all_kind, current_hand_kind);

  const std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> round_end_pt_exp =
      cal_round_end_pt_exp(game_record, game_state, my_pid, false, tactics);
  const std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> round_end_pt_exp_ar =
      game_state.player_state[my_pid].riichi_accepted
          ? round_end_pt_exp
          : cal_round_end_pt_exp(game_record, game_state, my_pid, true, tactics);
  const std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> drawn_round_pt_exp =
      cal_drawn_round_pt_exp(game_record, game_state, my_pid, false, tactics);
  const std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> drawn_round_pt_exp_ar =
      game_state.player_state[my_pid].riichi_accepted
          ? drawn_round_pt_exp
          : cal_drawn_round_pt_exp(game_record, game_state, my_pid, true, tactics);

  if (console_out) {
    std::cout << "drawn_round_pt_exp:" << std::endl;
    for (int t0 = 0; t0 < 2; t0++) {
      for (int t1 = 0; t1 < 2; t1++) {
        for (int t2 = 0; t2 < 2; t2++) {
          for (int t3 = 0; t3 < 2; t3++) {
            std::cout << t0 << " " << t1 << " " << t2 << " " << t3 << " "
                      << drawn_round_pt_exp[t0][t1][t2][t3] << " "
                      << drawn_round_pt_exp_ar[t0][t1][t2][t3] << std::endl;
          }
        }
      }
    }
  }

  Hand_Analyzer hand_analyzer, hand_analyzer_af;
  if (console_out) {
    std::cout << "current_tile:" << current_tile << std::endl;
    std::cout << "current_open_meld_count:" << current_open_meld.size() << std::endl;
    for (int tile = 0; tile < 38; tile++) {
      for (int i = 0; i < current_hand[tile]; i++) {
        std::cout << tile << " ";
      }
    }
    std::cout << std::endl;
  }
  hand_analyzer.reset_hand_analyzer_with(current_hand, current_riichi, current_open_meld);
  if (console_out) {
    hand_analyzer.print_hand();
  }

  if (current_riichi && current_action.type == EventType::DRAW) {
    hand_analyzer.delete_tile(current_tile);
  }
  hand_analyzer.analyze_tenpai(my_pid, game_state);
  const int meld_shanten_num = hand_analyzer.get_meld_shanten_num();
  const int seven_pairs_shanten_num = hand_analyzer.get_seven_pairs_shanten_num();
  const bool rule_base = hand_analyzer.rule_base_decision(my_pid);
  if (console_out) {
    std::cout << "shanten_num:" << meld_shanten_num << " " << seven_pairs_shanten_num << " "
              << (int)rule_base << std::endl;
  }

  std::array<Tenpai_Estimator_Simple, 4> tenpai_estimator;
  for (int pid = 0; pid < 4; pid++) {
    tenpai_estimator[pid].set_tenpai_estimator(game_record, game_state, my_pid, pid, tactics);
  }
  if (console_out) {
    std::cout << "tenpai_prob:";
    for (int pid = 0; pid < 4; pid++) {
      std::cout << tenpai_estimator[pid].tenpai_prob << " ";
    }
    std::cout << std::endl;
  }

  const std::pair<std::array<std::array<float, 38>, 4>, std::array<std::array<float, 38>, 4>>
      deal_in_tile_prob_value =
          cal_deal_in_tile_prob_value(tenpai_estimator, round_end_pt_exp, my_pid, false);
  const std::pair<std::array<std::array<float, 38>, 4>, std::array<std::array<float, 38>, 4>>
      deal_in_tile_prob_value_now =
          cal_deal_in_tile_prob_value(tenpai_estimator, round_end_pt_exp, my_pid, true);
  const std::array<std::array<float, 38>, 4> &deal_in_tile_prob = deal_in_tile_prob_value.first;
  const std::array<std::array<float, 38>, 4> &deal_in_tile_value = deal_in_tile_prob_value.second;
  const std::array<std::array<float, 38>, 4> &deal_in_tile_prob_now =
      deal_in_tile_prob_value_now.first;
  const std::array<std::array<float, 38>, 4> &deal_in_tile_value_now =
      deal_in_tile_prob_value_now.second;
  const std::array<std::array<std::array<float, 12>, 14>, 4> tsumo_hanfu_prob =
      cal_win_hanfu_prob_array(tenpai_estimator, true);
  const std::array<std::array<std::array<float, 12>, 14>, 4> ron_hanfu_prob =
      cal_win_hanfu_prob_array(tenpai_estimator, false);
  const std::array<std::array<std::array<float, 12>, 14>, 4> tsumo_hanfu_prob_kan =
      cal_hanfu_prob_kan(tsumo_hanfu_prob, tactics.han_shift_prob_kan);

  const TotalDealIn total_deal_in_tile_prob_value = cal_total_deal_in_tile_prob_value(
      my_pid, tenpai_estimator, deal_in_tile_prob, deal_in_tile_value);
  const std::array<float, 38> &total_deal_in_tile_prob = total_deal_in_tile_prob_value.probability;
  const std::array<float, 38> &total_deal_in_tile_value = total_deal_in_tile_prob_value.value;

  const TotalDealIn total_deal_in_tile_prob_value_now = cal_total_deal_in_tile_prob_value(
      my_pid, tenpai_estimator, deal_in_tile_prob_now, deal_in_tile_value_now);
  const std::array<float, 38> &total_deal_in_tile_prob_now =
      total_deal_in_tile_prob_value_now.probability;
  const std::array<float, 38> &total_deal_in_tile_value_now =
      total_deal_in_tile_prob_value_now.value;

  if (console_out) {
    for (int pid = 0; pid < 4; pid++) {
      std::cout << "deal_in_prob:" << pid << std::endl;
      for (int i = 1; i <= 7; i++) {
        printf("%5.4f %5.4f %5.4f %5.4f\n", deal_in_tile_prob[pid][i],
               deal_in_tile_prob[pid][10 + i], deal_in_tile_prob[pid][20 + i],
               deal_in_tile_prob[pid][30 + i]);
      }
      printf("%5.4f %5.4f %5.4f\n", deal_in_tile_prob[pid][8], deal_in_tile_prob[pid][10 + 8],
             deal_in_tile_prob[pid][20 + 8]);
      printf("%5.4f %5.4f %5.4f\n", deal_in_tile_prob[pid][9], deal_in_tile_prob[pid][10 + 9],
             deal_in_tile_prob[pid][20 + 9]);
    }
    for (int pid = 0; pid < 4; pid++) {
      std::cout << "deal_in_tile_value:" << pid << std::endl;
      for (int i = 1; i <= 7; i++) {
        printf("%5.4f %5.4f %5.4f %5.4f\n", deal_in_tile_value[pid][i],
               deal_in_tile_value[pid][10 + i], deal_in_tile_value[pid][20 + i],
               deal_in_tile_value[pid][30 + i]);
      }
      printf("%5.4f %5.4f %5.4f\n", deal_in_tile_value[pid][8], deal_in_tile_value[pid][10 + 8],
             deal_in_tile_value[pid][20 + 8]);
      printf("%5.4f %5.4f %5.4f\n", deal_in_tile_value[pid][9], deal_in_tile_value[pid][10 + 9],
             deal_in_tile_value[pid][20 + 9]);
    }
    std::cout << "total_deal_in:" << std::endl;
    for (int tile = 0; tile < 38; tile++) {
      std::cout << tile << " " << total_deal_in_tile_prob[tile] << " "
                << total_deal_in_tile_value[tile] << " " << total_deal_in_tile_prob_now[tile] << " "
                << total_deal_in_tile_value_now[tile] << std::endl;
    }
  }

  const std::array<float, 4> tenpai_prob_array = get_tenpai_prob_array(tenpai_estimator);
  const float passive_drawn_round_prob =
      cal_drawn_round_prob(my_pid, game_state, 0.0, tenpai_prob_array, 0);
  const double passive_drawn_round_value =
      cal_passive_drawn_round_value(my_pid, game_state, tenpai_prob_array, drawn_round_pt_exp);
  const double not_win_value = cal_exp(my_pid, game_record, game_state, current_hand, 0.0, 0.0, 0.0,
                                       0.0, tenpai_prob_array, deal_in_tile_prob, tsumo_hanfu_prob,
                                       ron_hanfu_prob, round_end_pt_exp, drawn_round_pt_exp, 0, 0);
  const double other_end_value = cal_other_end_value(
      my_pid, game_state, tenpai_prob_array, tsumo_hanfu_prob, ron_hanfu_prob, round_end_pt_exp);
  const double other_end_value_ar =
      tactics.other_end_ar
          ? cal_other_end_value(my_pid, game_state, tenpai_prob_array, tsumo_hanfu_prob,
                                ron_hanfu_prob, round_end_pt_exp_ar)
          : other_end_value;
  const double other_end_value_kan =
      cal_other_end_value(my_pid, game_state, tenpai_prob_array, tsumo_hanfu_prob_kan,
                          ron_hanfu_prob, round_end_pt_exp);

  // TODO: Use the correct discard increment for draw and open-meld decisions.
  const int tsumo_num_exp = std::min(cal_tsumo_num_exp(my_pid, game_state, 1, tenpai_prob_array),
                                     cal_tsumo_num_DP(game_record, my_pid));
  assert(tsumo_num_exp >= 0);

  if (console_out) {
    std::cout << "passive_drawn_round_prob:" << passive_drawn_round_prob << std::endl;
    std::cout << "other_end_value:" << other_end_value << " " << other_end_value_ar << " "
              << other_end_value_kan << std::endl;
  }

  if (rule_base) {
    if (current_action.type == EventType::DRAW) {
      if (tactics.do_nine_terminals) {
        const Event nine_terminals = make_nine_terminals(my_pid);
        if (is_legal_nine_terminals(game_record, game_state, nine_terminals)) {
          Tile_Choice tile_choice_tmp;
          tile_choice_tmp.action_type = AT_NINE_TERMINALS;
          tile_choice.push_back(tile_choice_tmp);
          assert(tile_choice.size() == 1);
          return;
        }
      }
      bool is_fold = false;
      for (int pid_add = 1; pid_add < 4; pid_add++) {
        const int pid = next_player(my_pid, pid_add);
        if (game_state.player_state[pid].riichi_declared ||
            tenpai_estimator[pid].tenpai_prob > 0.5) {
          is_fold = true;
        }
      }
      if (is_fold) {
        for (int tile = 0; tile < 38; tile++) {
          if (game_state.player_state[my_pid].hand[tile] > 0) {
            Tile_Choice tile_choice_tmp;
            tile_choice_tmp.action_type = AT_DISCARD;
            tile_choice_tmp.tile = tile;

            Tile_Array hand_tmp = current_hand;
            hand_tmp[tile]--;
            std::array<float, 38> full_defense_deal_in_tile_prob = total_deal_in_tile_prob;
            full_defense_deal_in_tile_prob[tile] = 0.0;

            tile_choice_tmp.pt_exp_after_fold =
                cal_full_defense(hand_tmp, full_defense_deal_in_tile_prob, total_deal_in_tile_value,
                                 other_end_value, passive_drawn_round_value, tsumo_num_exp)
                    .full_defense_exp;
            tile_choice_tmp.pt_exp_total =
                total_deal_in_tile_prob_now[tile] * total_deal_in_tile_value_now[tile] +
                (1.0 - total_deal_in_tile_prob_now[tile]) * tile_choice_tmp.pt_exp_after_fold;
            tile_choice_tmp.review.set_total_deal_in_tile_prob_now(
                total_deal_in_tile_prob_now[tile]);
            tile_choice_tmp.review.set_total_deal_in_tile_weighted_utility_now(
                total_deal_in_tile_prob_value_now.weighted_utility[tile]);
            if (total_deal_in_tile_prob_now[tile] != 0) {
              tile_choice_tmp.review.set_pt_exp_after(tile_choice_tmp.pt_exp_after_fold);
            }
            tile_choice_tmp.review.set_pt_exp_total(tile_choice_tmp.pt_exp_total);
            tile_choice.push_back(tile_choice_tmp);
          }
        }
      } else {
        int tile_tmp = 0;
        tile_tmp = isolated_tile_most_needless(
            game_state.player_state[my_pid].hand, tile_visible_all, game_state.round_wind,
            game_state.player_state[my_pid].self_wind, game_state.dora_marker);
        Tile_Choice tile_choice_tmp;
        tile_choice_tmp.action_type = AT_DISCARD;
        tile_choice_tmp.tile = tile_tmp;
        tile_choice.push_back(tile_choice_tmp);
      }
      std::sort(tile_choice.rbegin(), tile_choice.rend());
    } else {
      Open_Meld_Choice open_meld_choice_tmp;
      open_meld_choice_tmp.open_meld_action_type = AT_OPEN_MELD_PASS;
      open_meld_choice.push_back(open_meld_choice_tmp);
    }
    return;
  }

  if (current_riichi) {
    hand_analyzer.meld_change_num_max = 0;
    hand_analyzer.seven_pairs_change_num_max = 0;
  } else {
    hand_analyzer.pattern = 1;
    hand_analyzer.meld_change_num_max =
        hand_analyzer.get_meld_shanten_num() +
        tactics.hand_change_count[hand_analyzer.get_meld_shanten_num()];
    hand_analyzer.seven_pairs_change_num_max =
        cal_seven_pairs_change_num_max(seven_pairs_shanten_num, meld_shanten_num);
    hand_analyzer.analyze_tenpai(my_pid, game_state);
  }

  hand_analyzer_af.reset_hand_analyzer_with(current_hand, current_riichi, current_open_meld);

  if (current_action.type == EventType::DISCARD && !current_riichi) {
    hand_analyzer_af.add_tile(current_tile);
    hand_analyzer_af.analyze_tenpai(my_pid, game_state);
    hand_analyzer_af.pattern = 1;
    if (hand_analyzer_af.get_meld_shanten_num() <= 1) {
      hand_analyzer_af.meld_change_num_max = hand_analyzer_af.get_meld_shanten_num() + 2;
    } else if (hand_analyzer_af.get_meld_shanten_num() <= 2) {
      hand_analyzer_af.meld_change_num_max = hand_analyzer_af.get_meld_shanten_num() + 1;
    } else if (hand_analyzer_af.get_meld_shanten_num() <= 3) {
      hand_analyzer_af.meld_change_num_max = hand_analyzer_af.get_meld_shanten_num();
    }
    hand_analyzer_af.seven_pairs_change_num_max = 0;
    hand_analyzer_af.analyze_tenpai(my_pid, game_state);
  }

  Hand_Calculator hand_calculator;
  hand_calculator.reset(my_pid, get_red_dora(game_record));
  hand_calculator.open_meld_cand_tile =
      current_action.type == EventType::DISCARD ? current_tile : 0;
  hand_calculator.get_effective(game_state, hand_analyzer);
  hand_calculator.hand_all_num =
      hand_analyzer.get_hand_num() + hand_analyzer.get_open_meld_num() * 3;

  if (console_out) {
    std::cout << "shanten:" << hand_analyzer.get_shanten_num() << std::endl;
    for (int i = 0; i < 9; i++) {
      std::cout << i << " " << hand_analyzer.inout_pattern_vec[i].size() << std::endl;
    }
    std::cout << "candidates_num:" << hand_calculator.candidates_size() << std::endl;
  }
  hand_calculator.set_candidates3_single_thread(
      game_state, hand_analyzer, hand_analyzer_af,
      std::max({hand_analyzer.meld_change_num_max, hand_analyzer.seven_pairs_change_num_max,
                hand_analyzer_af.meld_change_num_max}),
      tactics);
  hand_analyzer.pattern = 0;

  const std::array<bool, 38> discard_kind = get_discard_kind(game_state.player_state[my_pid].river);
  hand_calculator.set_candidates3_multi_thread(game_record, game_state, discard_kind, hand_analyzer,
                                               tactics);

  const bool chii_action = current_action.type == EventType::DISCARD
                               ? ((current_action.player + 1) % 4 == my_pid)
                               : false;
  const int open_meld_win_shanten_num =
      hand_calculator.get_open_meld_win_shanten_num(game_state, current_hand, chii_action);
  const bool cal_dp = should_cal_dp(hand_analyzer.get_shanten_num(), open_meld_win_shanten_num,
                                    get_other_riichi_declared_num(my_pid, game_state) > 0,
                                    current_action.type == EventType::DISCARD, tactics);

  if (cal_dp) {
    const int fold_choice_mode = 2;
    const int tsumo_num_DP = cal_tsumo_num_DP(game_record, my_pid);

    if (game_state.player_state[0].riichi_declared || game_state.player_state[1].riichi_declared ||
        game_state.player_state[2].riichi_declared || game_state.player_state[3].riichi_declared ||
        meld_shanten_num > 0) {
      hand_calculator.calc_DP(game_state.player_state[my_pid].river.size(), tsumo_num_DP,
                              other_end_value, other_end_value_ar, other_end_value_kan,
                              tenpai_prob_array, deal_in_tile_prob, deal_in_tile_value,
                              fold_choice_mode, 1.0, 0.0, passive_drawn_round_prob,
                              tile_visible_all_kind, game_state, round_end_pt_exp,
                              drawn_round_pt_exp, drawn_round_pt_exp_ar, tactics);
    } else {
      hand_calculator.calc_DP(game_state.player_state[my_pid].river.size(), tsumo_num_DP,
                              other_end_value, other_end_value_ar, other_end_value_kan,
                              tenpai_prob_array, deal_in_tile_prob, deal_in_tile_value,
                              fold_choice_mode, 1.0, tenpai_prob_array[my_pid],
                              passive_drawn_round_prob, tile_visible_all_kind, game_state,
                              round_end_pt_exp, drawn_round_pt_exp, drawn_round_pt_exp_ar, tactics);
    }

    if (current_action.type == EventType::DRAW) {
      for (int cn = 0; cn < hand_calculator.in0num; cn++) {
        for (int gn = 0; gn < hand_calculator.group_size(cn); gn++) {
          if (is_same_open_meld(hand_calculator.get_const_proto_sequence_cgn(cn, gn).hand_state,
                                hand_calculator.get_const_proto_sequence_cgn(cn, 0).hand_state)) {
            const int tile =
                find_tile_out_proto_sequence(game_state.player_state[my_pid].hand,
                                             hand_calculator.get_const_proto_sequence_cgn(cn, gn));
            if (tile == 0) continue;
            Tile_Choice tile_choice_tmp;
            tile_choice_tmp.action_type = AT_DISCARD;
            tile_choice_tmp.tile = tile;
            if (tile_kind(tile) == tile_kind(current_tile) &&
                (hand_calculator.get_const_proto_sequence_cgn(cn, gn).get_riichi() == 1) ==
                    game_state.player_state[my_pid].riichi_accepted) {
              Tile_Choice tsumo_win_choice;
              tsumo_win_choice.tile = current_tile;
              tsumo_win_choice.pt_exp_total = -200;
              const std::array<int, 3> &win_loc = hand_calculator_work.get_const_win_loc(cn, gn);
              for (int an = win_loc[1]; an < win_loc[2]; an++) {
                const Win_Calc &win = hand_calculator_work.win_graph_work[win_loc[0]][an];
                if (tile_kind(current_tile) == win.win_info.get_tile()) {
                  const int last_draw_han = (count_tsumo_num_all(game_record) == 70 ? 1 : 0);
                  if (win.win_info.get_han_tsumo() + last_draw_han > 0) {
                    tsumo_win_choice.action_type = AT_TSUMO_WIN;
                    const std::array<double, 4> points_exp = win.get_points_exp(
                        my_pid, hand_calculator.get_const_proto_sequence_cgn(cn, gn).hand_bit,
                        hand_calculator.get_const_proto_sequence_cgn(cn, gn).hand_state,
                        tile_visible_kind, game_state, round_end_pt_exp, last_draw_han);
                    const double win_exp =
                        is_red_tile(current_tile) ? points_exp[2] : points_exp[0];
                    if (win_exp > tsumo_win_choice.pt_exp_total) {
                      tsumo_win_choice.pt_exp_total = win_exp;
                    }
                  }
                }
              }
              if (tsumo_win_choice.action_type == AT_TSUMO_WIN) {
                tile_choice.push_back(tsumo_win_choice);
              }
            }

            if (hand_calculator.get_const_proto_sequence_cgn(cn, gn).get_riichi() == 1) {
              tile_choice_tmp.action_type = AT_RIICHI_DECLARE;
            }
            tile_choice_tmp.pt_exp_after_fold = hand_calculator.get_fold_exp(cn, gn, tsumo_num_DP);
            tile_choice_tmp.pt_exp_after = hand_calculator.get_points_exp(cn, gn, tsumo_num_DP);

            tile_choice.push_back(tile_choice_tmp);

            if (count_tsumo_num_all(game_record) < 70) {
              const std::array<int, 3> &tsumo_edge_loc =
                  hand_calculator_work.get_const_tsumo_edge_loc(cn, gn);
              for (int acn = tsumo_edge_loc[1]; acn < tsumo_edge_loc[2]; acn++) {
                const Hand_Action &ac_tmp =
                    hand_calculator_work.cand_graph_sub_tsumo_work[tsumo_edge_loc[0]][acn];
                if (ac_tmp.tile_out == tile && (ac_tmp.action_type == AT_CONCEALED_KAN ||
                                                ac_tmp.action_type == AT_UPGRADED_KAN)) {
                  if (game_state.player_state[my_pid].riichi_accepted &&
                      !hand_calculator.get_const_proto_sequence_cgn(cn, gn)
                           .can_concealed_kan_after_riichi(tile)) {
                    continue;
                  }
                  Tile_Choice kan_choice;
                  kan_choice.tile = tile;
                  kan_choice.action_type = ac_tmp.action_type;
                  // It can be AT_CONCEALED_KAN_AND_RIICHI_DECLARE. We keep ac_tmp.action_type
                  // because the move output is tedious. Note: the same moves for AT_CONCEALED_KAN
                  // can be output with different scores.

                  kan_choice.pt_exp_after_fold = hand_calculator.get_fold_exp(
                      ac_tmp.dst_group, ac_tmp.dst_group_sub, tsumo_num_DP);
                  kan_choice.pt_exp_after = hand_calculator.get_points_exp(
                      ac_tmp.dst_group, ac_tmp.dst_group_sub, tsumo_num_DP);
                  tile_choice.push_back(kan_choice);
                }
              }
            }
          }
        }
      }
      for (int i = 0; i < tile_choice.size(); i++) {
        if (is_concealed_kan(tile_choice[i].action_type)) {
          tile_choice[i].pt_exp_total = tile_choice[i].pt_exp_after;
        } else if (tile_choice[i].action_type != AT_TSUMO_WIN) {
          const int tile_out = tile_choice[i].tile;
          tile_choice[i].pt_exp_total =
              total_deal_in_tile_prob_now[tile_out] * total_deal_in_tile_value_now[tile_out] +
              (1.0 - total_deal_in_tile_prob_now[tile_out]) * tile_choice[i].pt_exp_after;
          tile_choice[i].review.set_total_deal_in_tile_prob_now(
              total_deal_in_tile_prob_now[tile_out]);
          tile_choice[i].review.set_total_deal_in_tile_weighted_utility_now(
              total_deal_in_tile_prob_value_now.weighted_utility[tile_out]);
          if (total_deal_in_tile_prob_now[tile_out] != 0) {
            tile_choice[i].review.set_pt_exp_after(tile_choice[i].pt_exp_after);
          }
        }
        tile_choice[i].review.set_pt_exp_total(tile_choice[i].pt_exp_total);
      }
      if (console_out) {
        for (int i = 0; i < tile_choice.size(); i++) {
          std::cout << "tile_choice:" << tile_choice[i].tile << " " << tile_choice[i].pt_exp_total
                    << " " << tile_choice[i].pt_exp_after_fold << " " << tile_choice[i].pt_exp_after
                    << " ";
          std::cout << total_deal_in_tile_prob_now[tile_choice[i].tile] << " "
                    << total_deal_in_tile_value_now[tile_choice[i].tile] << std::endl;
        }
      }
      std::sort(tile_choice.rbegin(), tile_choice.rend());
    } else {
      Open_Meld_Choice open_meld_choice_tmp;

      int cn_open_meld_neg = 0;
      int gn_open_meld_neg = 0;
      for (int gn = 0; gn < hand_calculator.group_size(cn_open_meld_neg); gn++) {
        if (is_same_hand_proto_sequence(
                current_hand, hand_calculator.get_const_proto_sequence_cgn(cn_open_meld_neg, gn))) {
          gn_open_meld_neg = gn;
          break;
        }
      }

      double ron_pt_exp = -200.0;
      bool same_turn_furiten = false;
      const std::array<bool, 38> furiten_flags =
          get_furiten_flags(game_record, game_state, my_pid, true);
      if (hand_calculator.get_const_proto_sequence_cgn(cn_open_meld_neg, gn_open_meld_neg)
              .get_furiten() == 0) {
        const std::array<int, 3> &win_loc =
            hand_calculator_work.get_const_win_loc(cn_open_meld_neg, gn_open_meld_neg);
        for (int an = win_loc[1]; an < win_loc[2]; an++) {
          const Win_Calc &win = hand_calculator_work.win_graph_work[win_loc[0]][an];
          if (furiten_flags[win.win_info.get_tile()]) {
            same_turn_furiten = true;
          }
          if (tile_kind(current_tile) == win.win_info.get_tile()) {
            const int incident_han = (count_tsumo_num_all(game_record) == 70 ? 1 : 0) +
                                     (current_action.type == EventType::UPGRADED_KAN ? 1 : 0);
            if (win.win_info.get_han_ron() || 0 < incident_han) {
              open_meld_choice_tmp.open_meld_action_type = AT_RON_WIN;
              open_meld_choice_tmp.open_meld_tile = current_tile;
              const std::array<double, 2> points_exp = win.get_points_exp_direct(
                  my_pid, current_action.player, incident_han,
                  hand_calculator.get_const_proto_sequence_cgn(cn_open_meld_neg, gn_open_meld_neg)
                      .hand_bit,
                  hand_calculator.get_const_proto_sequence_cgn(cn_open_meld_neg, gn_open_meld_neg)
                      .hand_state,
                  tile_visible_kind, game_state, round_end_pt_exp);
              const double win_exp = is_red_tile(current_tile) ? points_exp[1] : points_exp[0];
              if (win_exp > ron_pt_exp) {
                ron_pt_exp = win_exp;
                open_meld_choice_tmp.pt_exp_total = ron_pt_exp;
              }
            }
          }
        }
      }

      if (open_meld_choice_tmp.open_meld_action_type == AT_RON_WIN && !same_turn_furiten) {
        open_meld_choice.push_back(open_meld_choice_tmp);
      }

      open_meld_choice_tmp.open_meld_action_type = AT_OPEN_MELD_PASS;
      // TODO: Compare not_win_value with passive_drawn_round_value for this decision.
      open_meld_choice_tmp.pt_exp_total =
          std::max(hand_calculator.get_points_exp(cn_open_meld_neg, gn_open_meld_neg, tsumo_num_DP),
                   not_win_value);
      open_meld_choice.push_back(open_meld_choice_tmp);

      if (current_action.type == EventType::DISCARD &&
          count_tsumo_num_all(game_record) < 70) {  // This prevents an open meld on the last draw.
        const std::array<int, 3> &open_meld_edge_loc =
            hand_calculator_work.get_const_open_meld_edge_loc(cn_open_meld_neg, gn_open_meld_neg);
        for (int acn = open_meld_edge_loc[1]; acn < open_meld_edge_loc[2]; acn++) {
          const Hand_Action &ac_tmp =
              hand_calculator_work.cand_graph_sub_open_meld_work[open_meld_edge_loc[0]][acn];
          if (ac_tmp.tile != current_tile) {
            continue;
          } else if (my_pid != next_player(current_action.player, 1) &&
                     is_chii(ac_tmp.action_type)) {
            continue;
          } else {
            open_meld_choice_tmp.reset();
            open_meld_choice_tmp.open_meld_tile = ac_tmp.tile;
            assert(ac_tmp.tile == current_tile);
            open_meld_choice_tmp.open_meld_action_type = ac_tmp.action_type;
            open_meld_choice_tmp.tile_out = ac_tmp.tile_out;
            set_exposed_tile(
                hand_calculator.get_const_proto_sequence_cgn(ac_tmp.dst_group, ac_tmp.dst_group_sub)
                    .hand_state,
                game_state.player_state[my_pid].open_meld, current_tile,
                open_meld_choice_tmp.exposed_tile);

            open_meld_choice_tmp.pt_exp_after = hand_calculator.get_points_exp(
                ac_tmp.dst_group, ac_tmp.dst_group_sub, std::max(tsumo_num_DP - 1, 0));
            open_meld_choice_tmp.pt_exp_after_prev = hand_calculator.get_points_exp(
                ac_tmp.dst_group, ac_tmp.dst_group_sub, std::max(tsumo_num_DP, 0));

            open_meld_choice.push_back(open_meld_choice_tmp);
          }
        }
      }

      for (int i = 0; i < open_meld_choice.size(); i++) {
        if (open_meld_choice[i].open_meld_action_type != AT_OPEN_MELD_PASS &&
            open_meld_choice[i].open_meld_action_type != AT_RON_WIN) {
          const int tile_out = open_meld_choice[i].tile_out;
          open_meld_choice[i].pt_exp_total =
              total_deal_in_tile_prob_now[tile_out] * total_deal_in_tile_value_now[tile_out] +
              (1.0 - total_deal_in_tile_prob_now[tile_out]) * open_meld_choice[i].pt_exp_after;
          open_meld_choice[i].pt_exp_total_prev =
              total_deal_in_tile_prob_now[tile_out] * total_deal_in_tile_value_now[tile_out] +
              (1.0 - total_deal_in_tile_prob_now[tile_out]) * open_meld_choice[i].pt_exp_after_prev;
          open_meld_choice[i].pt_exp_total =
              std::min(open_meld_choice[i].pt_exp_total, open_meld_choice[i].pt_exp_total_prev);
          open_meld_choice[i].review.set_total_deal_in_tile_prob_now(
              total_deal_in_tile_prob_now[tile_out]);
          open_meld_choice[i].review.set_total_deal_in_tile_weighted_utility_now(
              total_deal_in_tile_prob_value_now.weighted_utility[tile_out]);
          if (total_deal_in_tile_prob_now[tile_out] != 0) {
            open_meld_choice[i].review.set_pt_exp_after(open_meld_choice[i].pt_exp_after);
          }
        }
        open_meld_choice[i].review.set_pt_exp_total(open_meld_choice[i].pt_exp_total);
      }
      if (console_out) {
        for (int i = 0; i < open_meld_choice.size(); i++) {
          std::cout << "open_meld_choice:" << (int)open_meld_choice[i].open_meld_action_type << " "
                    << open_meld_choice[i].tile_out << " " << open_meld_choice[i].pt_exp_total
                    << " " << open_meld_choice[i].pt_exp_total_prev << std::endl;
        }
      }

      std::sort(open_meld_choice.rbegin(), open_meld_choice.rend());
    }
  } else {
    if (console_out) {
      std::cout << "not_win_value:" << not_win_value << std::endl;
      std::cout << "other_end_value:" << other_end_value << std::endl;
      std::cout << "passive_drawn_round_value:" << passive_drawn_round_value << std::endl;
    }
    hand_calculator.calc_win_prob(tsumo_num_exp, other_end_value, game_state,
                                  round_end_pt_exp);  // This matches the reference implementation.
                                                      // Check whether other_end_value is correct.
    if (console_out) {
      std::cout << "calc_win_prob_input:" << other_end_value << " " << tsumo_num_exp << std::endl;
    }
    if (current_action.type == EventType::DRAW) {
      for (int cn = 0; cn < hand_calculator.in0num; cn++) {
        for (int gn = 0; gn < hand_calculator.group_size(cn); gn++) {
          if (is_ts_open_meld_consistent(
                  hand_calculator.get_const_proto_sequence_cgn(cn, gn).hand_state,
                  game_state.player_state[my_pid].open_meld)) {
            const int tile_out =
                find_tile_out_proto_sequence(game_state.player_state[my_pid].hand,
                                             hand_calculator.get_const_proto_sequence_cgn(cn, gn));
            if (tile_out == 0) continue;
            Tile_Choice tile_choice_tmp;
            tile_choice_tmp.tile = tile_out;
            tile_choice_tmp.action_type =
                hand_calculator.get_const_proto_sequence_cgn(cn, gn).get_riichi()
                    ? AT_RIICHI_DECLARE
                    : AT_DISCARD;

            const auto &round_end_pt_exp_tmp = tile_choice_tmp.action_type == AT_RIICHI_DECLARE
                                                   ? round_end_pt_exp_ar
                                                   : round_end_pt_exp;
            const auto &drawn_round_pt_exp_tmp = tile_choice_tmp.action_type == AT_RIICHI_DECLARE
                                                     ? drawn_round_pt_exp_ar
                                                     : drawn_round_pt_exp;

            Tile_Array hand_tmp = current_hand;
            hand_tmp[tile_out]--;
            tile_choice_tmp.pt_exp_after =
                cal_exp(my_pid, game_record, game_state, hand_tmp,
                        hand_calculator.get_win_prob(cn, gn, tsumo_num_exp),
                        hand_calculator.get_points_gain(cn, gn, tsumo_num_exp), other_end_value,
                        hand_calculator.get_tenpai_prob(cn, gn, tsumo_num_exp), tenpai_prob_array,
                        deal_in_tile_prob, tsumo_hanfu_prob, ron_hanfu_prob, round_end_pt_exp_tmp,
                        drawn_round_pt_exp_tmp, 1, 0);
            if (console_out) {
              std::cout << cn << " " << gn << " " << tile_out << " "
                        << hand_calculator.get_win_prob(cn, gn, tsumo_num_exp) << " "
                        << tile_choice_tmp.pt_exp_after << std::endl;
            }

            std::array<float, 38> full_defense_deal_in_tile_prob = total_deal_in_tile_prob;
            const int discarded_kind = tile_kind(tile_out);
            full_defense_deal_in_tile_prob[discarded_kind] = 0.0;
            if (discarded_kind < 30 && discarded_kind % 10 == 5)
              full_defense_deal_in_tile_prob[discarded_kind + 5] = 0.0;
            tile_choice_tmp.pt_exp_after_fold = cal_full_defense_exp(
                my_pid, game_state, hand_tmp, full_defense_deal_in_tile_prob,
                total_deal_in_tile_value, not_win_value, other_end_value, passive_drawn_round_value,
                passive_drawn_round_prob, tsumo_num_exp, tactics);
            tile_choice.push_back(tile_choice_tmp);

            if (count_tsumo_num_all(game_record) < 70) {
              const std::array<int, 3> &tsumo_edge_loc =
                  hand_calculator_work.get_const_tsumo_edge_loc(cn, gn);
              for (int acn = tsumo_edge_loc[1]; acn < tsumo_edge_loc[2]; acn++) {
                const Hand_Action &ac_tmp =
                    hand_calculator_work.cand_graph_sub_tsumo_work[tsumo_edge_loc[0]][acn];
                if (ac_tmp.tile_out == tile_out && (ac_tmp.action_type == AT_CONCEALED_KAN ||
                                                    ac_tmp.action_type == AT_UPGRADED_KAN)) {
                  if (game_state.player_state[my_pid].riichi_accepted &&
                      !hand_calculator.get_const_proto_sequence_cgn(cn, gn)
                           .can_concealed_kan_after_riichi(tile_out)) {
                    continue;
                  }
                  Tile_Choice kan_choice;
                  kan_choice.tile = tile_out;
                  kan_choice.action_type = ac_tmp.action_type;
                  Tile_Array kan_hand = hand_tmp;
                  kan_hand[tile_out] = 0;
                  kan_hand[tile_kind(tile_out)] = 0;
                  // other_end_value is the reference value for the backward analysis. We must not
                  // change it to other_end_value_kan. Decide whether discard_inc is 0.
                  kan_choice.pt_exp_after =
                      cal_exp(my_pid, game_record, game_state, kan_hand,
                              hand_calculator.get_win_prob(ac_tmp.dst_group, ac_tmp.dst_group_sub,
                                                           tsumo_num_exp),
                              hand_calculator.get_points_gain(ac_tmp.dst_group,
                                                              ac_tmp.dst_group_sub, tsumo_num_exp),
                              other_end_value,
                              hand_calculator.get_tenpai_prob(ac_tmp.dst_group,
                                                              ac_tmp.dst_group_sub, tsumo_num_exp),
                              tenpai_prob_array, deal_in_tile_prob, tsumo_hanfu_prob_kan,
                              ron_hanfu_prob, round_end_pt_exp_tmp, drawn_round_pt_exp_tmp, 0, 0);
                  kan_choice.pt_exp_after_fold = cal_full_defense_exp(
                      my_pid, game_state, kan_hand, full_defense_deal_in_tile_prob,
                      total_deal_in_tile_value, not_win_value, other_end_value,
                      passive_drawn_round_value, passive_drawn_round_prob, tsumo_num_exp, tactics);
                  tile_choice.push_back(kan_choice);
                }
              }
            }
          }
        }
      }

      if (0 < get_other_riichi_declared_num(my_pid, game_state) ||
          2 <= get_max_other_open_meld_num(my_pid, game_state)) {
        // We must verify which full-defense condition makes the AI stronger.
        for (int i = 0; i < tile_choice.size(); i++) {
          if (tile_choice[i].pt_exp_after_fold > tile_choice[i].pt_exp_after) {
            tile_choice[i].pt_exp_after = tile_choice[i].pt_exp_after_fold;
          }
        }
      }

      for (int i = 0; i < tile_choice.size(); i++) {
        const int tile_out = tile_choice[i].tile;
        if (tile_choice[i].action_type == AT_DISCARD ||
            tile_choice[i].action_type == AT_RIICHI_DECLARE ||
            tile_choice[i].action_type == AT_UPGRADED_KAN) {
          tile_choice[i].pt_exp_total =
              total_deal_in_tile_prob_now[tile_out] * total_deal_in_tile_value_now[tile_out] +
              (1.0 - total_deal_in_tile_prob_now[tile_out]) * tile_choice[i].pt_exp_after;
        } else if (tile_choice[i].action_type == AT_CONCEALED_KAN) {
          tile_choice[i].pt_exp_total = tile_choice[i].pt_exp_after;
        } else {
          assert_with_out(false, "selector tile_choice action_type error");
        }

        tile_choice[i].review.set_total_deal_in_tile_prob_now(
            total_deal_in_tile_prob_now[tile_out]);
        tile_choice[i].review.set_total_deal_in_tile_weighted_utility_now(
            total_deal_in_tile_prob_value_now.weighted_utility[tile_out]);
        if (total_deal_in_tile_prob_now[tile_out] != 0) {
          tile_choice[i].review.set_pt_exp_after(tile_choice[i].pt_exp_after);
        }
        tile_choice[i].review.set_pt_exp_total(tile_choice[i].pt_exp_total);
        if (console_out) {
          std::cout << "tile_choice:" << tile_choice[i].tile << " " << tile_choice[i].pt_exp_total
                    << " " << tile_choice[i].pt_exp_after_fold << " " << tile_choice[i].pt_exp_after
                    << std::endl;
          std::cout << total_deal_in_tile_prob_now[tile_choice[i].tile] << " "
                    << total_deal_in_tile_value_now[tile_choice[i].tile] << std::endl;
        }
      }
      std::sort(tile_choice.rbegin(), tile_choice.rend());
    } else {
      open_meld_choice.erase(open_meld_choice.begin(), open_meld_choice.end());

      int cn_open_meld_neg = 0;
      int gn_open_meld_neg = 0;
      for (int gn = 0; gn < hand_calculator.group_size(cn_open_meld_neg); gn++) {
        if (is_same_hand_proto_sequence(
                current_hand, hand_calculator.get_const_proto_sequence_cgn(cn_open_meld_neg, gn))) {
          gn_open_meld_neg = gn;
          break;
        }
      }

      Open_Meld_Choice pass_choice;
      pass_choice.open_meld_action_type = AT_OPEN_MELD_PASS;

      pass_choice.pt_exp_after = cal_exp(
          my_pid, game_record, game_state, current_hand,
          hand_calculator.get_win_prob(cn_open_meld_neg, gn_open_meld_neg, tsumo_num_exp),
          hand_calculator.get_points_gain(cn_open_meld_neg, gn_open_meld_neg, tsumo_num_exp),
          other_end_value,
          hand_calculator.get_tenpai_prob(cn_open_meld_neg, gn_open_meld_neg, tsumo_num_exp),
          tenpai_prob_array, deal_in_tile_prob, tsumo_hanfu_prob, ron_hanfu_prob, round_end_pt_exp,
          drawn_round_pt_exp, 0, 0);

      open_meld_choice.push_back(pass_choice);

      if (current_action.type == EventType::DISCARD &&
          count_tsumo_num_all(game_record) < 70) {  // This prevents an open meld on the last draw.
        const std::array<int, 3> &open_meld_edge_loc =
            hand_calculator_work.get_const_open_meld_edge_loc(cn_open_meld_neg, gn_open_meld_neg);
        for (int acn = open_meld_edge_loc[1]; acn < open_meld_edge_loc[2]; acn++) {
          const Hand_Action &ac_tmp =
              hand_calculator_work.cand_graph_sub_open_meld_work[open_meld_edge_loc[0]][acn];
          if (ac_tmp.action_type != AT_TSUMO) {
            if (ac_tmp.tile != current_tile) {
              continue;
            } else if (my_pid != next_player(current_action.player, 1) &&
                       is_chii(ac_tmp.action_type)) {
              continue;
            } else {
              Open_Meld_Choice open_meld_choice_tmp;
              open_meld_choice_tmp.open_meld_tile = ac_tmp.tile;
              open_meld_choice_tmp.open_meld_action_type = ac_tmp.action_type;
              open_meld_choice_tmp.tile_out = ac_tmp.tile_out;

              // TODO: Replace exposed_tile with a vector.
              for (int i = 0; i < 3; i++) {
                open_meld_choice_tmp.exposed_tile[i] = 0;
              }
              set_exposed_tile(
                  hand_calculator
                      .get_const_proto_sequence_cgn(ac_tmp.dst_group, ac_tmp.dst_group_sub)
                      .hand_state,
                  game_state.player_state[my_pid].open_meld, current_tile,
                  open_meld_choice_tmp.exposed_tile);
              if (console_out) {
                std::cout << "open_meld_candidate:" << (int)ac_tmp.action_type << " "
                          << (int)ac_tmp.tile_out << " "
                          << hand_calculator.get_win_prob(ac_tmp.dst_group, ac_tmp.dst_group_sub,
                                                          tsumo_num_exp)
                          << std::endl;
              }
              Tile_Array hand_tmp = current_hand;
              for (int i = 0; i < 3; i++) {
                hand_tmp[open_meld_choice_tmp.exposed_tile[i]]--;
              }
              hand_tmp[open_meld_choice_tmp.tile_out]--;
              open_meld_choice_tmp.pt_exp_after =
                  cal_exp(my_pid, game_record, game_state, hand_tmp,
                          hand_calculator.get_win_prob(ac_tmp.dst_group, ac_tmp.dst_group_sub,
                                                       tsumo_num_exp - 1),
                          hand_calculator.get_points_gain(ac_tmp.dst_group, ac_tmp.dst_group_sub,
                                                          tsumo_num_exp - 1),
                          other_end_value,
                          hand_calculator.get_tenpai_prob(ac_tmp.dst_group, ac_tmp.dst_group_sub,
                                                          tsumo_num_exp - 1),
                          tenpai_prob_array, deal_in_tile_prob, tsumo_hanfu_prob, ron_hanfu_prob,
                          round_end_pt_exp, drawn_round_pt_exp, 1, 1);
              open_meld_choice_tmp.pt_exp_after_prev =
                  cal_exp(my_pid, game_record, game_state, hand_tmp,
                          hand_calculator.get_win_prob(ac_tmp.dst_group, ac_tmp.dst_group_sub,
                                                       tsumo_num_exp),
                          hand_calculator.get_points_gain(ac_tmp.dst_group, ac_tmp.dst_group_sub,
                                                          tsumo_num_exp),
                          other_end_value,
                          hand_calculator.get_tenpai_prob(ac_tmp.dst_group, ac_tmp.dst_group_sub,
                                                          tsumo_num_exp),
                          tenpai_prob_array, deal_in_tile_prob, tsumo_hanfu_prob, ron_hanfu_prob,
                          round_end_pt_exp, drawn_round_pt_exp, 0, 1);
              // TODO: Also use one fewer draw for values outside the hand calculator.
              open_meld_choice.push_back(open_meld_choice_tmp);
            }
          }
        }
      }

      for (int i = 0; i < open_meld_choice.size(); i++) {
        if (open_meld_choice[i].open_meld_action_type == AT_OPEN_MELD_PASS) {
          open_meld_choice[i].pt_exp_total = open_meld_choice[i].pt_exp_after;
        } else if (open_meld_choice[i].open_meld_action_type != AT_RON_WIN) {
          const int tile_out = open_meld_choice[i].tile_out;
          open_meld_choice[i].pt_exp_total =
              total_deal_in_tile_prob_now[tile_out] * total_deal_in_tile_value_now[tile_out] +
              (1.0 - total_deal_in_tile_prob_now[tile_out]) * open_meld_choice[i].pt_exp_after;
          open_meld_choice[i].pt_exp_total_prev =
              total_deal_in_tile_prob_now[tile_out] * total_deal_in_tile_value_now[tile_out] +
              (1.0 - total_deal_in_tile_prob_now[tile_out]) * open_meld_choice[i].pt_exp_after_prev;
          open_meld_choice[i].pt_exp_total =
              std::min(open_meld_choice[i].pt_exp_total, open_meld_choice[i].pt_exp_total_prev);
          open_meld_choice[i].review.set_total_deal_in_tile_prob_now(
              total_deal_in_tile_prob_now[tile_out]);
          open_meld_choice[i].review.set_total_deal_in_tile_weighted_utility_now(
              total_deal_in_tile_prob_value_now.weighted_utility[tile_out]);
          if (total_deal_in_tile_prob_now[tile_out] != 0) {
            open_meld_choice[i].review.set_pt_exp_after(open_meld_choice[i].pt_exp_after);
          }
        }
        open_meld_choice[i].review.set_pt_exp_total(open_meld_choice[i].pt_exp_total);
        if (console_out) {
          std::cout << "open_meld_choice:" << (int)open_meld_choice[i].open_meld_action_type << " "
                    << open_meld_choice[i].tile_out << " " << open_meld_choice[i].pt_exp_total
                    << " " << open_meld_choice[i].pt_exp_total_prev << " ";
          if (open_meld_choice[i].open_meld_action_type != AT_OPEN_MELD_PASS &&
              open_meld_choice[i].open_meld_action_type != AT_RON_WIN) {
            std::cout << open_meld_choice[i].pt_exp_after << " "
                      << total_deal_in_tile_prob_now[open_meld_choice[i].tile_out] << " "
                      << total_deal_in_tile_value_now[open_meld_choice[i].tile_out] << std::endl;
          } else {
            std::cout << std::endl;
          }
        }
      }
      std::sort(open_meld_choice.rbegin(), open_meld_choice.rend());
    }
  }
}

Moves ai(const Moves &game_record, const int pid, const bool console_out_input) {
  assert(game_record.size() > 0);
  console_out = console_out_input;
  const Event &last_action = game_record[game_record.size() - 1];
  const Tactics &tactics = tactics_all[pid];

  Selector selector;
  selector.set_selector(game_record, pid, tactics);
  if (console_out) {
    std::cout << "ai:" << pid << " " << selector.tile_choice.size() << " "
              << selector.open_meld_choice.size() << std::endl;
  }
  if (selector.tile_choice.size() > 0) {
    const Game_State game_state = get_game_state(game_record);
    if (console_out) {
      std::cout << moves_to_string(
                       selector.tile_choice[0].get_moves(game_state, pid, last_action.tile))
                << std::endl;
    }
    return selector.tile_choice[0].get_moves(game_state, pid, last_action.tile);
  } else if (selector.open_meld_choice.size() > 0) {
    if (console_out) {
      std::cout << "open_meld_choice:" << (int)selector.open_meld_choice[0].open_meld_action_type
                << " " << selector.open_meld_choice[0].open_meld_tile << std::endl;
      std::cout << moves_to_string(selector.open_meld_choice[0].get_moves(pid, last_action.player))
                << std::endl;
    }
    return selector.open_meld_choice[0].get_moves(pid, last_action.player);
  } else {
    return {make_none(pid)};
  }
}

std::vector<std::pair<Moves, float>> calc_moves_score(const Moves &game_record, const int pid) {
  assert(game_record.size() > 0);
  console_out = false;
  const Event &last_action = game_record[game_record.size() - 1];
  const Tactics &tactics = tactics_all[pid];

  std::vector<std::pair<Moves, float>> ret;
  Selector selector;
  selector.set_selector(game_record, pid, tactics);
  if (selector.tile_choice.size() > 0) {
    const Game_State game_state = get_game_state(game_record);
    for (const Tile_Choice &choice : selector.tile_choice) {
      ret.push_back(std::pair<Moves, float>(
          {choice.get_moves(game_state, pid, last_action.tile), choice.pt_exp_total}));
    }
  } else if (selector.open_meld_choice.size() > 0) {
    for (const Open_Meld_Choice &choice : selector.open_meld_choice) {
      ret.push_back(std::pair<Moves, float>(
          {choice.get_moves(pid, last_action.player), choice.pt_exp_total}));
    }
  }
  return ret;
}

std::vector<Decision_Score> ai_review(const Moves &game_record, const int pid) {
  assert(game_record.size() > 0);
  console_out = false;
  const Event &last_action = game_record[game_record.size() - 1];
  const Tactics &tactics = tactics_all[pid];

  std::vector<Decision_Score> decisions;
  Selector selector;
  selector.set_selector(game_record, pid, tactics);
  if (selector.tile_choice.size() > 0) {
    const Game_State game_state = get_game_state(game_record);
    for (const Tile_Choice &choice : selector.tile_choice) {
      decisions.push_back({choice.get_moves(game_state, pid, last_action.tile), choice.review});
    }
  } else if (selector.open_meld_choice.size() > 0) {
    for (const Open_Meld_Choice &choice : selector.open_meld_choice) {
      decisions.push_back({choice.get_moves(pid, last_action.player), choice.review});
    }
  }

  return decisions;
}

void set_tactics(const std::array<Tactics, 4> &tactics) { tactics_all = tactics; }
