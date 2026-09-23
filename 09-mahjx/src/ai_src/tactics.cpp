#include "tactics.hpp"

int cal_seven_pairs_change_num_max(const int seven_pairs_shanten_num, const int meld_shanten_num) {
  if (seven_pairs_shanten_num <= meld_shanten_num) {
    if (seven_pairs_shanten_num <= 2) {
      return seven_pairs_shanten_num + 1;
    } else {
      return seven_pairs_shanten_num;
    }
  } else if (seven_pairs_shanten_num + 1 <= meld_shanten_num) {
    return seven_pairs_shanten_num;
  } else {
    return 0;
  }
}

bool should_cal_dp(const int shanten_num, const int open_meld_win_shanten_num,
                   const bool is_other_riichi_declared, const bool is_open_meld_phase,
                   const Tactics &tactics) {
  if (shanten_num <= tactics.inclusive_shanten_num_always) {
    return true;
  }
  if (is_other_riichi_declared) {
    if (shanten_num <= tactics.inclusive_shanten_num_other_riichi) {
      return true;
    }
  }
  if (is_open_meld_phase && shanten_num <= tactics.inclusive_shanten_num_open_meld) {
    return true;
  }
  if (is_other_riichi_declared && is_open_meld_phase) {
    if (shanten_num <= tactics.inclusive_shanten_num_open_meld_other_riichi) {
      return true;
    }
  }

  if (open_meld_win_shanten_num <= tactics.inclusive_shanten_num_always) {
    return true;
  }
  if (is_other_riichi_declared) {
    if (open_meld_win_shanten_num <= tactics.inclusive_shanten_num_other_riichi) {
      return true;
    }
  }

  if (open_meld_win_shanten_num <= tactics.inclusive_shanten_num_open_meld) {
    return true;
  }

  if (is_other_riichi_declared && is_open_meld_phase) {
    if (open_meld_win_shanten_num <= tactics.inclusive_shanten_num_open_meld_other_riichi) {
      return true;
    }
  }
  return false;
}
