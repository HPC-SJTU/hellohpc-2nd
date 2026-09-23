#pragma once

#include "../share/include.hpp"

class Tactics {
 public:
  std::array<int, 4> turn_pt;
  int hand_change_count[7];  // Set the number of hand changes to consider for each shanten count.

  bool consider_kan;       // Consider kan in hand_calculator.
  bool do_nine_terminals;  // Declare nine terminals.

  int win_coeff_tp_fnm;
  int win_coeff_tp_an;
  bool other_end_ar;
  int tenpai_after_est_begin;

  bool fold_exp_at_dp_open_meld;  // In the deal-in probability calculation, also use fold_exp when
                                  // the open meld count increases.

  int inclusive_shanten_num_always;  // The shanten count for using the inclusive single-player
                                     // mahjong policy.
  int inclusive_shanten_num_other_riichi;  // The shanten count for using the inclusive
                                           // single-player mahjong policy when another player
                                           // declares riichi.
  int inclusive_shanten_num_open_meld;
  int inclusive_shanten_num_open_meld_other_riichi;

  int max_open_meld_num;

  std::array<std::array<double, 12>, 14> hanfu_weight_tsumo;
  std::array<std::array<double, 12>, 14> hanfu_weight_ron;
  std::array<float, 14> han_shift_prob_kan;

  Tactics() = default;
};

int cal_seven_pairs_change_num_max(const int seven_pairs_shanten_num, const int meld_shanten_num);
bool should_cal_dp(const int shanten_num, const int open_meld_win_shanten_num,
                   const bool is_other_riichi_declared, const bool is_open_meld_phase,
                   const Tactics &tactics);
