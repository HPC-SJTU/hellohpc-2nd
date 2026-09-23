#pragma once

#include "../share/include.hpp"
#include "mahjong_util.hpp"
#include "tactics.hpp"

std::array<std::array<float, 4>, 4> calc_turn_prob_end(const std::array<int, 4> &points,
                                                       const int origin_dealer);
std::array<float, 24> infer_game_result_prob(const std::array<int, 4> &score,
                                             const int ranking_model_round);
std::array<int, 16> score_list_to_4onehot(const std::array<int, 4> &score);

bool dealer_end_game(const std::array<int, 4> &points, const int dealer_id);
bool dealer_end_game_w4(const std::array<int, 4> &points, const int dealer_id);
std::array<std::array<float, 4>, 4> calc_turn_prob(const int ranking_model_round,
                                                   const std::array<int, 4> &points,
                                                   const int dealer, const bool is_dealer_repeat,
                                                   const Tactics &tactics);
