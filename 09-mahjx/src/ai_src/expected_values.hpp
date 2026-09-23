#pragma once

#include "../share/include.hpp"
#include "full_defense.hpp"
#include "mahjong_util.hpp"

float get_drawn_round_pattern_prob(const std::array<int, 4> &flag,
                                   const std::array<float, 4> &formal_tenpai_prob);
int cal_tsumo_num_exp(
    const int my_pid, const Game_State &game_state, const int discard_inc,
    const std::array<float, 4> tenpai_prob);  // Estimate the number of remaining actions.

float cal_my_win_prob(
    const int my_pid, const Moves &game_record, const float win_prob_sol,
    const Game_State &game_state, const int discard_inc,
    const std::array<float, 4> &tenpai_prob);  // Estimate the win probability from the
                                               // single-player result of the win policy.
float cal_my_win_value(const double win_prob_sol, const double points_gain,
                       const double value_not_win);  // Estimate the win value from the
                                                     // single-player result of the win policy.

float cal_drawn_round_prob(
    const int my_pid, const Game_State &game_state, const float my_win_prob,
    const std::array<float, 4> &tenpai_prob,
    const int discard_inc);  // Probability of a drawn round when we cannot win.
float cal_my_formal_tenpai_prob(
    const int my_pid, const Game_State &game_state, const int discard_inc,
    const float formal_tenpai_prob_sol);  // Probability of being tenpai when the round is drawn.
float cal_other_formal_tenpai_prob(const int my_pid, const int target_pid,
                                   const Game_State &game_state, const int discard_inc,
                                   const float current_tenpai_prob);
std::array<float, 4> cal_formal_tenpai_prob(const int my_pid, const Game_State &game_state,
                                            const int discard_inc,
                                            const float formal_tenpai_prob_sol,
                                            const std::array<float, 4> &tenpai_prob);
float cal_drawn_round_value(
    const std::array<float, 4> &formal_tenpai_prob,
    const std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> &drawn_round_pt_exp);
float cal_not_ready_drawn_round_value(
    const int my_pid, const std::array<float, 4> &formal_tenpai_prob,
    const std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> &drawn_round_pt_exp);

std::array<std::array<float, 4>, 4> cal_target_prob(
    const int my_pid, const std::array<float, 4> &risk_array, const Game_State &game_state,
    const int discard_inc,
    const int open_meld_num_inc);  // Estimate which player a winner wins from.
std::array<std::array<float, 4>, 4> cal_target_prob_other(
    const int my_pid);  // Estimate which player a winner wins from. This assumes that we do not
                        // participate.

double cal_target_value_child(const std::array<std::array<float, 12>, 14> &round_end_pt_exp,
                              const std::array<std::array<float, 12>, 14> &han_prob);

std::array<std::array<double, 4>, 4> cal_target_value(
    const int my_pid,
    const std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> &round_end_pt_exp,
    const float my_win_value,
    const std::array<std::array<std::array<float, 12>, 14>, 4> &tsumo_prob,
    const std::array<std::array<std::array<float, 12>, 14>, 4>
        &ron_prob);  // Value when a player wins from another player.

std::array<float, 5> cal_round_result_prob(
    const int my_pid, const Game_State &game_state, const int discard_inc, const float my_win_prob,
    const std::array<float, 4> &tenpai_prob,
    const float drawn_round_prob);  // Estimate the win rate of each player.

std::array<double, 5> cal_round_result_value(
    const std::array<std::array<float, 4>, 4> &target_prob,
    const std::array<std::array<double, 4>, 4> &target_value, const float drawn_round_value);

std::array<float, 4> cal_risk_array(const int my_pid, const Game_State &game_state,
                                    const int discard_inc, const Tile_Array &hand,
                                    const std::array<float, 4> tenpai_prob,
                                    const std::array<std::array<float, 38>, 4> &deal_in_tile_prob);

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
    const int discard_inc, const int open_meld_inc);

float cal_passive_drawn_round_value(
    const int my_pid, const Game_State &game_state, const std::array<float, 4> &tenpai_prob,
    const std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> &drawn_round_pt_exp);

double cal_other_end_value(
    const int my_pid, const Game_State &game_state, const std::array<float, 4> &tenpai_prob,
    const std::array<std::array<std::array<float, 12>, 14>, 4> &tsumo_prob,
    const std::array<std::array<std::array<float, 12>, 14>, 4> &ron_prob,
    const std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> &round_end_pt_exp);

float cal_full_defense_exp(const int my_pid, const Game_State &game_state,
                           const Tile_Array &hand_tmp,
                           const std::array<float, 38> &full_defense_deal_in_tile_prob,
                           const std::array<float, 38> &total_deal_in_tile_value,
                           const float not_win_value, const float other_end_value,
                           const float passive_drawn_round_value,
                           const float passive_drawn_round_prob, const int tsumo_num_exp,
                           const Tactics &tactics);
