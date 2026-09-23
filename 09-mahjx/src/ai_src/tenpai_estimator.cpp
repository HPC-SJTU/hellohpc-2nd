#include "tenpai_estimator.hpp"

extern const bool console_out;
bool value_tile_check_est(const int value_tile, const Open_Meld_Vector &open_meld,
                          const int triplet[38], const int wait_tile, const Wait_Type wait_type) {
  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].consumed[0] == value_tile) {
      return true;
    }
  }
  if (triplet[value_tile] == 1) {
    return true;
  } else if (wait_type == MT_DUAL_PAIR && wait_tile == value_tile) {
    return true;
  } else {
    return false;
  }
}

bool full_flush_check_est(const Open_Meld_Vector &open_meld, const int head[38],
                          const int triplet[38], const int sequence[30], const int wait_tile) {
  bool flags[CT_HONOR_TILE];
  for (Color_Type color = CT_CHARACTERS; color < CT_HONOR_TILE; ++color) {
    flags[color] = true;
  }
  for (Color_Type color = CT_CHARACTERS; color < CT_HONOR_TILE; ++color) {
    if (tile_color(wait_tile) != color) {
      flags[color] = false;
      continue;
    }
    for (int fn = 0; fn < open_meld.size(); fn++) {
      if (tile_color(open_meld[fn].consumed[0]) != color) {
        flags[color] = false;
        break;
      }
    }
    if (flags[color]) {
      for (int tile = 0; tile < 38; tile++) {
        if ((head[tile] > 0 || triplet[tile] > 0 || (tile < 30 && sequence[tile] > 0)) &&
            tile_color(tile) != color) {
          flags[color] = false;
          break;
        }
      }
    }
  }
  return flags[CT_CHARACTERS] || flags[CT_DOTS] || flags[CT_BAMBOO];
}

bool half_flush_check_est(const Open_Meld_Vector &open_meld, const int head[38],
                          const int triplet[38], const int sequence[30], const int wait_tile) {
  bool flags[CT_HONOR_TILE];
  for (Color_Type color = CT_CHARACTERS; color < CT_HONOR_TILE; ++color) {
    flags[color] = true;
  }
  for (Color_Type color = CT_CHARACTERS; color < CT_HONOR_TILE; ++color) {
    if (tile_color(wait_tile) != color && tile_color(wait_tile) != CT_HONOR_TILE) {
      flags[color] = false;
      continue;
    }
    for (int fn = 0; fn < open_meld.size(); fn++) {
      if (tile_color(open_meld[fn].consumed[0]) != color &&
          tile_color(open_meld[fn].consumed[0]) != CT_HONOR_TILE) {
        flags[color] = false;
        break;
      }
    }
    if (flags[color]) {
      for (int tile = 0; tile < 30; tile++) {
        if ((head[tile] > 0 || triplet[tile] > 0 || (tile < 30 && sequence[tile] > 0)) &&
            tile_color(tile) != color) {
          flags[color] = false;
          break;
        }
      }
    }
  }
  return flags[CT_CHARACTERS] || flags[CT_DOTS] || flags[CT_BAMBOO];
}

bool simples_check_est(const Open_Meld_Vector &open_meld, const int head[38], const int triplet[38],
                       const int sequence[30], const int wait_tile, const Wait_Type wait_type) {
  for (int hc = 0; hc < 3; hc++) {
    if (head[hc * 10 + 1] > 0 || head[hc * 10 + 9] > 0 || triplet[hc * 10 + 1] > 0 ||
        triplet[hc * 10 + 9] > 0 || sequence[hc * 10 + 1] > 0 || sequence[hc * 10 + 7] > 0) {
      return false;
    }
  }
  for (int tile = 31; tile < 38; tile++) {
    if (head[tile] > 0 || triplet[tile] > 0) {
      return false;
    }
  }
  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].type == FT_CHII) {
      if (tile_terminal_or_honor(open_meld[fn].tile) != NT_SIMPLES ||
          tile_terminal_or_honor(open_meld[fn].consumed[0]) != NT_SIMPLES ||
          tile_terminal_or_honor(open_meld[fn].consumed[1]) != NT_SIMPLES) {
        return false;
      }
    } else {
      if (tile_terminal_or_honor(open_meld[fn].consumed[0]) != NT_SIMPLES) {
        return false;
      }
    }
  }
  if (tile_terminal_or_honor(wait_tile) != NT_SIMPLES) {
    return false;
  }
  if (wait_type == MT_EDGE_WAIT) {
    return false;
  }
  if (wait_type == MT_KANCHAN && (wait_tile % 10 == 2 || wait_tile % 10 == 8)) {
    return false;
  }
  return true;
}

bool terminals_and_honors_check_est(const Open_Meld_Vector &open_meld, const int head[38],
                                    const int triplet[38], const int sequence[30],
                                    const int wait_tile, const Wait_Type wait_type) {
  if ((wait_type != MT_DUAL_PAIR && wait_type != MT_PAIR_WAIT) ||
      tile_terminal_or_honor(wait_tile) == NT_SIMPLES) {
    return false;
  }
  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (tile_terminal_or_honor(open_meld[fn].consumed[0]) == NT_SIMPLES ||
        tile_terminal_or_honor(open_meld[fn].consumed[1]) == NT_SIMPLES) {
      return false;
    }
  }
  for (int tile = 0; tile < 30; tile++) {
    if (sequence[tile] > 0) {
      return false;
    }
    if (tile_terminal_or_honor(tile) == NT_SIMPLES && (head[tile] > 0 || triplet[tile] > 0)) {
      return false;
    }
  }
  return true;
}

bool suitsu_check_est(const Open_Meld_Vector &open_meld, const int head[38], const int triplet[38],
                      const int sequence[30], const int wait_tile, const Wait_Type wait_type) {
  if ((wait_type != MT_DUAL_PAIR && wait_type != MT_PAIR_WAIT) ||
      tile_color(wait_tile) != CT_HONOR_TILE) {
    return false;
  }
  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].consumed[0] <= 30) {
      return false;
    }
  }
  for (int tile = 0; tile < 30; tile++) {
    if (head[tile] != 0 || triplet[tile] != 0 || sequence[tile] != 0) {
      return false;
    }
  }
  return true;
}

bool pure_outside_hand_check_est(const Open_Meld_Vector &open_meld, const int head[38],
                                 const int triplet[38], const int sequence[30], const int wait_tile,
                                 const Wait_Type wait_type) {
  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].type == FT_CHII) {
      if (tile_terminal_or_honor(open_meld[fn].tile) != NT_TERMINAL &&
          tile_terminal_or_honor(open_meld[fn].consumed[0]) != NT_TERMINAL &&
          tile_terminal_or_honor(open_meld[fn].consumed[1]) != NT_TERMINAL) {
        return false;
      }
    } else if (tile_terminal_or_honor(open_meld[fn].consumed[0]) != NT_TERMINAL) {
      return false;
    }
  }

  for (int tile = 0; tile < 38; tile++) {
    if ((head[tile] > 0 || triplet[tile] > 0) && tile_terminal_or_honor(tile) != NT_TERMINAL) {
      return false;
    }
  }

  for (int j = 0; j < 3; j++) {
    for (int i = 2; i <= 6; i++) {
      if (sequence[10 * j + i] > 0) {
        return false;
      }
    }
  }

  if (tile_terminal_or_honor(wait_tile) == NT_TERMINAL || wait_type == MT_EDGE_WAIT ||
      (wait_type == MT_KANCHAN && (wait_tile % 10 == 2 || wait_tile % 10 == 8))) {
    return true;
  }
  return false;
}

bool outside_hand_check_est(const Open_Meld_Vector &open_meld, const int head[38],
                            const int triplet[38], const int sequence[30], const int wait_tile,
                            const Wait_Type wait_type) {
  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].type == FT_CHII) {
      if (tile_terminal_or_honor(open_meld[fn].tile) == NT_SIMPLES &&
          tile_terminal_or_honor(open_meld[fn].consumed[0]) == NT_SIMPLES &&
          tile_terminal_or_honor(open_meld[fn].consumed[1]) == NT_SIMPLES) {
        return false;
      }
    } else if (tile_terminal_or_honor(open_meld[fn].consumed[0]) == NT_SIMPLES) {
      return false;
    }
  }

  for (int tile = 0; tile < 38; tile++) {
    if ((head[tile] > 0 || triplet[tile] > 0) && tile_terminal_or_honor(tile) == NT_SIMPLES) {
      return false;
    }
  }

  for (int j = 0; j < 3; j++) {
    for (int i = 2; i <= 6; i++) {
      if (sequence[10 * j + i] > 0) {
        return false;
      }
    }
  }

  if (tile_terminal_or_honor(wait_tile) != NT_SIMPLES || wait_type == MT_EDGE_WAIT ||
      (wait_type == MT_KANCHAN && (wait_tile % 10 == 2 || wait_tile % 10 == 8))) {
    return true;
  }
  return false;
}

bool all_triplets_check_est(const Open_Meld_Vector &open_meld, const int head[38],
                            const int triplet[38], const int sequence[30], const int wait_tile,
                            const Wait_Type wait_type) {
  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].type == FT_CHII) {
      return false;
    }
  }
  for (int tile = 0; tile < 30; tile++) {
    if (sequence[tile] > 0) {
      return false;
    }
  }
  return wait_type == MT_DUAL_PAIR || wait_type == MT_PAIR_WAIT;
}

bool three_color_triplet_check_est(const Open_Meld_Vector &open_meld, const int head[38],
                                   const int triplet[38], const int sequence[30],
                                   const int wait_tile, const Wait_Type wait_type) {
  int triplet_test[38];

  for (int tile = 0; tile < 38; tile++) {
    if (triplet[tile] > 0) {
      triplet_test[tile] = 1;
    } else {
      triplet_test[tile] = 0;
    }
  }

  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].type != FT_CHII) {
      triplet_test[open_meld[fn].consumed[0]]++;
    }
  }

  if (wait_type == MT_DUAL_PAIR) {
    triplet_test[wait_tile]++;
  }

  for (int i = 1; i <= 9; i++) {
    if (triplet_test[i] == 1 && triplet_test[10 + i] == 1 && triplet_test[20 + i] == 1) {
      return true;
    }
  }
  return false;
}

bool three_color_sequence_check_est(const Open_Meld_Vector &open_meld, const int head[38],
                                    const int triplet[38], const int sequence[30],
                                    const int wait_tile, const Wait_Type wait_type,
                                    const int open_wait_id) {
  int sequence_test[30];
  for (int tile = 0; tile < 30; tile++) {
    sequence_test[tile] = sequence[tile];
  }

  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].type == FT_CHII) {
      sequence_test[std::min({tile_kind(open_meld[fn].tile), tile_kind(open_meld[fn].consumed[0]),
                              tile_kind(open_meld[fn].consumed[1])})]++;
    }
  }

  if (wait_type == MT_OPEN_WAIT) {
    if (wait_tile == open_wait_id) {
      sequence_test[wait_tile]++;
    } else {
      sequence_test[wait_tile - 2]++;
    }
  } else if (wait_type == MT_KANCHAN) {
    sequence_test[wait_tile - 1]++;
  } else if (wait_type == MT_EDGE_WAIT) {
    if (wait_tile % 10 == 3) {
      sequence_test[wait_tile - 2]++;
    } else {
      sequence_test[wait_tile]++;
    }
  }

  for (int i = 1; i <= 7; i++) {
    if (sequence_test[i] > 0 && sequence_test[10 + i] > 0 && sequence_test[20 + i] > 0) {
      return true;
    }
  }
  return false;
}

bool full_straight_check_est(const Open_Meld_Vector &open_meld, const int head[38],
                             const int triplet[38], const int sequence[30], const int wait_tile,
                             const Wait_Type wait_type, const int open_wait_id) {
  int sequence_test[30];
  for (int tile = 0; tile < 30; tile++) {
    sequence_test[tile] = sequence[tile];
  }

  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].type == FT_CHII) {
      sequence_test[std::min({tile_kind(open_meld[fn].tile), tile_kind(open_meld[fn].consumed[0]),
                              tile_kind(open_meld[fn].consumed[1])})]++;
    }
  }

  if (wait_type == MT_OPEN_WAIT) {
    if (wait_tile == open_wait_id) {
      sequence_test[wait_tile]++;
    } else {
      sequence_test[wait_tile - 2]++;
    }
  } else if (wait_type == MT_KANCHAN) {
    sequence_test[wait_tile - 1]++;
  } else if (wait_type == MT_EDGE_WAIT) {
    if (wait_tile % 10 == 3) {
      sequence_test[wait_tile - 2]++;
    } else {
      sequence_test[wait_tile]++;
    }
  }

  for (int j = 0; j < 3; j++) {
    if (sequence_test[j * 10 + 1] > 0 && sequence_test[j * 10 + 4] > 0 &&
        sequence_test[j * 10 + 7] > 0) {
      return true;
    }
  }
  return false;
}

int concealed_triplet_num_count_est(const Open_Meld_Vector &open_meld, const int triplet[38]) {
  int concealed_triplet_num = 0;
  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].type == FT_CONCEALED_KAN) {
      concealed_triplet_num++;
    }
  }
  for (int tile = 0; tile < 38; tile++) {
    if (triplet[tile] == 1) {
      concealed_triplet_num++;
    }
  }
  return concealed_triplet_num;
}

int kan_num_count_est(const Open_Meld_Vector &open_meld) {
  int kan_num = 0;
  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].type == FT_OPEN_KAN || open_meld[fn].type == FT_CONCEALED_KAN ||
        open_meld[fn].type == FT_UPGRADED_KAN) {
      kan_num++;
    }
  }
  return kan_num;
}

Win_Estimate_Info::Win_Estimate_Info() {}

Win_Estimate_Info yaku_check(const int round_wind, const int self_wind,
                             const std::vector<int> &dora_vector, const Open_Meld_Vector open_meld,
                             const int visible_dora_num, const Tile_Array hand, const int head[38],
                             const int triplet[38], const int sequence[30], const int wait_tile,
                             const Wait_Type wait_type, const int open_wait_id) {
  Win_Estimate_Info win;
  win.tile = wait_tile;
  win.han = 0;
  win.fu = 0;
  if (full_flush_check_est(open_meld, head, triplet, sequence, wait_tile)) {
    win.han += 5;
  } else if (half_flush_check_est(open_meld, head, triplet, sequence, wait_tile)) {
    win.han += 2;
  }

  if (simples_check_est(open_meld, head, triplet, sequence, wait_tile, wait_type)) {
    win.han += 1;
  }

  if (terminals_and_honors_check_est(open_meld, head, triplet, sequence, wait_tile, wait_type)) {
    win.han += 2;
  }

  if (suitsu_check_est(open_meld, head, triplet, sequence, wait_tile, wait_type)) {
    win.han += 13;
  }

  bool has_dragon[3], has_wind[4];
  for (int i = 0; i < 3; i++) {
    has_dragon[i] = value_tile_check_est(35 + i, open_meld, triplet, wait_tile, wait_type);
  }
  for (int i = 0; i < 4; i++) {
    has_wind[i] = value_tile_check_est(31 + i, open_meld, triplet, wait_tile, wait_type);
  }

  if (has_wind[round_wind]) {
    win.han += 1;
  }

  if (has_wind[self_wind]) {
    win.han += 1;
  }

  for (int i = 0; i < 3; i++) {
    if (has_dragon[i]) {
      win.han += 1;
    }
  }

  bool pure_outside_hand = false;
  if (pure_outside_hand_check_est(open_meld, head, triplet, sequence, wait_tile, wait_type)) {
    win.han += 2;
    pure_outside_hand = true;
  } else if (outside_hand_check_est(open_meld, head, triplet, sequence, wait_tile, wait_type)) {
    win.han += 1;
  }

  bool all_triplets = false;
  if (all_triplets_check_est(open_meld, head, triplet, sequence, wait_tile, wait_type)) {
    win.han += 2;
    all_triplets = true;
  }

  if (three_color_triplet_check_est(open_meld, head, triplet, sequence, wait_tile, wait_type)) {
    win.han += 2;
  }

  if (three_color_sequence_check_est(open_meld, head, triplet, sequence, wait_tile, wait_type,
                                     open_wait_id)) {
    win.han += 1;
  }

  if (full_straight_check_est(open_meld, head, triplet, sequence, wait_tile, wait_type,
                              open_wait_id)) {
    win.han += 1;
  }

  int concealed_triplet_num = concealed_triplet_num_count_est(open_meld, triplet);
  if (concealed_triplet_num == 3) {
    win.han += 2;
  }

  int kan_num = kan_num_count_est(open_meld);
  if (kan_num == 3) {
    win.han += 2;
  } else if (kan_num == 4) {
    win.han += 13;
  }

  if (has_dragon[0] && has_dragon[1] && has_dragon[2]) {
    win.han += 13;
  } else {
    for (int i = 0; i < 3; i++) {
      if (has_dragon[(i + 1) % 3] && has_dragon[(i + 2) % 3] &&
          (head[35 + i] == 1 || wait_tile == 35 + i)) {
        win.han += 2;
      }
    }
  }

  if (has_wind[0] && has_wind[1] && has_wind[2] && has_wind[3]) {
    win.han += 13;
  } else {
    for (int i = 0; i < 4; i++) {
      if (has_wind[(i + 1) % 4] && has_wind[(i + 2) % 4] && has_wind[(i + 3) % 4] &&
          (head[31 + i] == 1 || wait_tile == 31 + i)) {
        win.han += 13;
      }
    }
  }

  if (pure_outside_hand == 1 && all_triplets == 1) {
    win.han = 13;
  }

  win.fu = 20;

  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].type == FT_PON) {
      if (tile_terminal_or_honor(tile_kind(open_meld[fn].consumed[0])) == NT_SIMPLES) {
        win.fu += 2;
      } else {
        win.fu += 4;
      }
    } else if (open_meld[fn].type == FT_OPEN_KAN || open_meld[fn].type == FT_UPGRADED_KAN) {
      if (tile_terminal_or_honor(tile_kind(open_meld[fn].consumed[0])) == NT_SIMPLES) {
        win.fu += 8;
      } else {
        win.fu += 16;
      }
    } else if (open_meld[fn].type == FT_CONCEALED_KAN) {
      if (tile_terminal_or_honor(tile_kind(open_meld[fn].consumed[0])) == NT_SIMPLES) {
        win.fu += 16;
      } else {
        win.fu += 32;
      }
    }
  }
  for (int tile = 1; tile < 38; tile++) {
    if (triplet[tile] == 1) {
      if (tile_terminal_or_honor(tile) == NT_SIMPLES) {
        win.fu += 4;
      } else {
        win.fu += 8;
      }
    } else if (head[tile] == 1 && wait_tile != tile) {
      if (tile >= 35 || tile == 31 + round_wind || tile == 31 + self_wind) {
        win.fu += 2;
      }
    }
  }

  if (wait_type >= 2) {
    win.fu = win.fu + 2;
  }
  if (wait_type == 2) {
    if (wait_tile >= 35 || wait_tile == 31 + round_wind || wait_tile == 31 + self_wind) {
      win.fu = win.fu + 2;
    }
  }

  int dora_num = visible_dora_num;
  for (int dn = 0; dn < dora_vector.size(); dn++) {
    dora_num += hand[dora_vector[dn]];
    if (dora_vector[dn] == wait_tile) {
      dora_num++;
    }
  }

  if (win.han > 0) {
    win.han += dora_num;
  }

  if (win.han >= 13) {
    win.han = 13;
  }

  return win;
}

Hand_Estimator_Element::Hand_Estimator_Element() {}

Hand_Estimator_Element::Hand_Estimator_Element(Tile_Array hand_in) { reset(hand_in); }

void Hand_Estimator_Element::reset(Tile_Array hand_in) {
  hand = hand_in;
  prob = 0.0;
  prob_now = 0.0;
  weight_units = 0;
  admitted_now = false;
  win_vector.clear();
}

Color_Type Hand_Estimator_Element::get_flushing_color(const Open_Meld_Vector &open_meld) {
  for (Color_Type color = (Color_Type)0; color < CT_NUM; ++color) {
    if (flushing_possible(open_meld, color)) {
      bool flag = true;
      for (int tile = 0; tile <= 30; tile++) {
        if (0 < hand[tile] && tile_color(tile) != color) {
          flag = false;
          break;
        }
      }
      if (flag) {
        return color;
      }
    }
  }
  return CT_NUM;
}

Hand_Estimator::Hand_Estimator() {}

void Hand_Estimator::tenpai_check(const int round_wind, const int self_wind,
                                  const std::vector<int> &dora_vector,
                                  const Open_Meld_Vector &open_meld, int type, Tile_Array &hand,
                                  int head[38], int triplet[38], int sequence[30],
                                  std::vector<Hand_Estimator_Element> &hand_expected_value) {
  Hand_Estimator_Element tee;
  if (type == 0) {
    for (int hc = 0; hc < 3; hc++) {
      for (int hn = 1; hn <= 6; hn++) {
        // open wait
        if (remaining[hc * 10 + hn + 1] - hand[hc * 10 + hn + 1] > 0 &&
            remaining[hc * 10 + hn + 2] - hand[hc * 10 + hn + 2] > 0) {
          hand[hc * 10 + hn + 1]++;
          hand[hc * 10 + hn + 2]++;
          tee.reset(hand);
          const bool low_exists = completing_tile_exists(hand, open_meld, hc * 10 + hn);
          const bool high_exists = completing_tile_exists(hand, open_meld, hc * 10 + hn + 3);
          if ((!low_exists || !decline_win[hc * 10 + hn]) &&
              (!high_exists || !decline_win[hc * 10 + hn + 3])) {
            if (low_exists)
              tee.win_vector.push_back(yaku_check(round_wind, self_wind, dora_vector, open_meld,
                                                  visible_dora_num, hand, head, triplet, sequence,
                                                  hc * 10 + hn, MT_OPEN_WAIT, hc * 10 + hn));
            if (high_exists)
              tee.win_vector.push_back(yaku_check(round_wind, self_wind, dora_vector, open_meld,
                                                  visible_dora_num, hand, head, triplet, sequence,
                                                  hc * 10 + hn + 3, MT_OPEN_WAIT, hc * 10 + hn));
            if (!tee.win_vector.empty() &&
                std::any_of(tee.win_vector.begin(), tee.win_vector.end(),
                            [](const Win_Estimate_Info &win) { return win.han > 0; })) {
              hand_expected_value.push_back(tee);
            }
          }
          hand[hc * 10 + hn + 1]--;
          hand[hc * 10 + hn + 2]--;
        }
      }
      for (int hn = 2; hn <= 8; hn++) {
        // kanchan
        if (remaining[hc * 10 + hn - 1] - hand[hc * 10 + hn - 1] > 0 &&
            remaining[hc * 10 + hn + 1] - hand[hc * 10 + hn + 1] > 0) {
          if (!decline_win[hc * 10 + hn] && completing_tile_exists(hand, open_meld, hc * 10 + hn)) {
            hand[hc * 10 + hn - 1]++;
            hand[hc * 10 + hn + 1]++;
            tee.reset(hand);
            tee.win_vector.push_back(yaku_check(round_wind, self_wind, dora_vector, open_meld,
                                                visible_dora_num, hand, head, triplet, sequence,
                                                hc * 10 + hn, MT_KANCHAN, 0));
            if (tee.win_vector[0].han > 0) {
              hand_expected_value.push_back(tee);
            }
            hand[hc * 10 + hn - 1]--;
            hand[hc * 10 + hn + 1]--;
          }
        }
      }
      // edge wait
      if (remaining[hc * 10 + 1] - hand[hc * 10 + 1] > 0 &&
          remaining[hc * 10 + 2] - hand[hc * 10 + 2] > 0) {
        if (!decline_win[hc * 10 + 3] && completing_tile_exists(hand, open_meld, hc * 10 + 3)) {
          hand[hc * 10 + 1]++;
          hand[hc * 10 + 2]++;
          tee.reset(hand);
          tee.win_vector.push_back(yaku_check(round_wind, self_wind, dora_vector, open_meld,
                                              visible_dora_num, hand, head, triplet, sequence,
                                              hc * 10 + 3, MT_EDGE_WAIT, 0));
          if (tee.win_vector[0].han > 0) {
            hand_expected_value.push_back(tee);
          }
          hand[hc * 10 + 1]--;
          hand[hc * 10 + 2]--;
        }
      }

      if (remaining[hc * 10 + 8] - hand[hc * 10 + 8] > 0 &&
          remaining[hc * 10 + 9] - hand[hc * 10 + 9] > 0) {
        if (!decline_win[hc * 10 + 7] && completing_tile_exists(hand, open_meld, hc * 10 + 7)) {
          hand[hc * 10 + 8]++;
          hand[hc * 10 + 9]++;
          tee.reset(hand);
          tee.win_vector.push_back(yaku_check(round_wind, self_wind, dora_vector, open_meld,
                                              visible_dora_num, hand, head, triplet, sequence,
                                              hc * 10 + 7, MT_EDGE_WAIT, 0));
          if (tee.win_vector[0].han > 0) {
            hand_expected_value.push_back(tee);
          }
          hand[hc * 10 + 8]--;
          hand[hc * 10 + 9]--;
        }
      }
    }
  } else if (type == 1) {
    tee.reset(hand);
    for (int tile = 0; tile < 38; tile++) {
      if (head[tile] == 1) {
        if (!completing_tile_exists(hand, open_meld, tile)) continue;
        if (decline_win[tile]) {
          return;
        }
        tee.win_vector.push_back(yaku_check(round_wind, self_wind, dora_vector, open_meld,
                                            visible_dora_num, hand, head, triplet, sequence, tile,
                                            MT_DUAL_PAIR, 0));
      }
    }
    if (!tee.win_vector.empty()) {
      if (std::any_of(tee.win_vector.begin(), tee.win_vector.end(),
                      [](const Win_Estimate_Info &win) { return win.han > 0; })) {
        hand_expected_value.push_back(tee);
      }
    }
  } else if (type == 2) {
    for (int tile = 0; tile < 38; tile++) {
      if (remaining[tile] - hand[tile] > 0) {
        if (!decline_win[tile]) {
          hand[tile]++;
          if (completing_tile_exists(hand, open_meld, tile)) {
            tee.reset(hand);
            tee.win_vector.push_back(yaku_check(round_wind, self_wind, dora_vector, open_meld,
                                                visible_dora_num, hand, head, triplet, sequence,
                                                tile, MT_PAIR_WAIT, 0));

            if (tee.win_vector[0].han > 0) {
              hand_expected_value.push_back(tee);
            }
          }
          hand[tile]--;
        }
      }
    }
  }
}

void Hand_Estimator::add_sequence(const int round_wind, const int self_wind,
                                  const std::vector<int> &dora_vector,
                                  const Open_Meld_Vector &open_meld, int head_num, int triplet_num,
                                  int sequence_num, int start, Tile_Array &hand, int head[38],
                                  int triplet[38], int sequence[30],
                                  std::vector<Hand_Estimator_Element> &hand_expected_value) {
  int meld_num = triplet_num + sequence_num;

  if (meld_num * 3 + head_num * 2 == hand_max_num - 1) {
    tenpai_check(round_wind, self_wind, dora_vector, open_meld, 2, hand, head, triplet, sequence,
                 hand_expected_value);
  } else if (meld_num * 3 + head_num * 2 == hand_max_num - 2) {
    tenpai_check(round_wind, self_wind, dora_vector, open_meld, 0, hand, head, triplet, sequence,
                 hand_expected_value);
  } else if (meld_num * 3 + head_num * 2 == hand_max_num) {
    tenpai_check(round_wind, self_wind, dora_vector, open_meld, 1, hand, head, triplet, sequence,
                 hand_expected_value);
  } else if (meld_num * 3 + head_num * 2 < hand_max_num) {
    for (int tile_color = start / 10; tile_color < 3; tile_color++) {
      for (int tile_index = start % 10; tile_index <= 7; tile_index++) {
        if (remaining[tile_color * 10 + tile_index] - hand[tile_color * 10 + tile_index] > 0 &&
            remaining[tile_color * 10 + tile_index + 1] - hand[tile_color * 10 + tile_index + 1] >
                0 &&
            remaining[tile_color * 10 + tile_index + 2] - hand[tile_color * 10 + tile_index + 2] >
                0) {
          hand[tile_color * 10 + tile_index]++;
          hand[tile_color * 10 + tile_index + 1]++;
          hand[tile_color * 10 + tile_index + 2]++;
          sequence[tile_color * 10 + tile_index]++;
          add_sequence(round_wind, self_wind, dora_vector, open_meld, head_num, triplet_num,
                       sequence_num + 1, tile_color * 10 + tile_index, hand, head, triplet,
                       sequence, hand_expected_value);
          hand[tile_color * 10 + tile_index]--;
          hand[tile_color * 10 + tile_index + 1]--;
          hand[tile_color * 10 + tile_index + 2]--;
          sequence[tile_color * 10 + tile_index]--;
        }
      }
    }
  }
}

void Hand_Estimator::add_triplet(const int round_wind, const int self_wind,
                                 const std::vector<int> &dora_vector,
                                 const Open_Meld_Vector &open_meld, int head_num, int triplet_num,
                                 int start, Tile_Array &hand, int head[38], int triplet[38],
                                 int sequence[30],
                                 std::vector<Hand_Estimator_Element> &hand_expected_value) {
  if (head_num * 2 + triplet_num * 3 < hand_max_num) {
    for (int tile = start; tile < 38; tile++) {
      if (remaining[tile] - hand[tile] >= 3) {
        hand[tile] += 3;
        triplet[tile]++;
        add_triplet(round_wind, self_wind, dora_vector, open_meld, head_num, triplet_num + 1,
                    tile + 1, hand, head, triplet, sequence, hand_expected_value);
        add_sequence(round_wind, self_wind, dora_vector, open_meld, head_num, triplet_num + 1, 0, 1,
                     hand, head, triplet, sequence, hand_expected_value);
        hand[tile] -= 3;
        triplet[tile]--;
      }
    }
  }
  add_sequence(round_wind, self_wind, dora_vector, open_meld, head_num, triplet_num, 0, 1, hand,
               head, triplet, sequence, hand_expected_value);
}

void Hand_Estimator::add_head(const int round_wind, const int self_wind,
                              const std::vector<int> &dora_vector,
                              const Open_Meld_Vector &open_meld, int head_num, int start,
                              Tile_Array &hand, int head[38], int triplet[38], int sequence[30],
                              std::vector<Hand_Estimator_Element> &hand_expected_value) {
  if (head_num < 2 && head_num * 2 < hand_max_num) {
    for (int tile = start; tile < 38; tile++) {
      if (remaining[tile] - hand[tile] >= 2) {
        hand[tile] += 2;
        head[tile]++;
        add_head(round_wind, self_wind, dora_vector, open_meld, head_num + 1, tile + 1, hand, head,
                 triplet, sequence, hand_expected_value);
        add_triplet(round_wind, self_wind, dora_vector, open_meld, head_num + 1, 0, 1, hand, head,
                    triplet, sequence, hand_expected_value);
        hand[tile] -= 2;
        head[tile]--;
      }
    }
  }
  add_triplet(round_wind, self_wind, dora_vector, open_meld, head_num, 0, 1, hand, head, triplet,
              sequence, hand_expected_value);
}

std::vector<Hand_Estimator_Element> Hand_Estimator::cal_hand_expected_value(
    const int round_wind, const int self_wind, const std::vector<int> &dora_marker,
    const Open_Meld_Vector &open_meld, const int hand_max_num_in, const Tile_Array &remaining_in,
    const std::array<bool, 38> &decline_win_in) {
  hand_max_num = hand_max_num_in;
  remaining = remaining_in;
  decline_win = decline_win_in;

  Tile_Array hand = {};
  int head[38] = {};
  int triplet[38] = {};
  int sequence[30] = {};
  visible_dora_num = count_dora(hand, open_meld, dora_marker);
  std::vector<Hand_Estimator_Element> hand_expected_value;
  const std::vector<int> dora_vector = dora_marker_to_dora(dora_marker);
  add_head(round_wind, self_wind, dora_vector, open_meld, 0, 1, hand, head, triplet, sequence,
           hand_expected_value);
  return hand_expected_value;
}

std::vector<Hand_Estimator_Element> Hand_Estimator::cal_hand_expected_value_with_prob(
    const Moves &game_record, const Game_State &game_state, const int target,
    const int hand_max_num_in, const Tile_Array &remaining_in,
    const std::array<bool, 38> &decline_win_in) {
  std::vector<Hand_Estimator_Element> hand_expected_value = cal_hand_expected_value(
      game_state.round_wind, game_state.player_state[target].self_wind, game_state.dora_marker,
      game_state.player_state[target].open_meld, hand_max_num_in, remaining_in, decline_win_in);
  const std::array<bool, 38> decline_win_flags = get_decline_win(game_record, target);
  for (int i = 0; i < hand_expected_value.size(); i++) {
    unsigned long long weight_units = 1;
    for (int tile = 0; tile < 38; tile++) {
      if (hand_expected_value[i].hand[tile] > 0) {
        weight_units *= combination(remaining_in[tile], hand_expected_value[i].hand[tile]);
      }
    }
    // Use fifths as the common exact mass unit. A single-wait candidate contributes one unit and
    // a two-wait candidate contributes five; the common factor cancels during normalization.
    weight_units *= hand_expected_value[i].win_vector.size() < 2 ? 1ULL : 5ULL;
    hand_expected_value[i].weight_units = weight_units;
    hand_expected_value[i].prob = static_cast<float>(weight_units);

    hand_expected_value[i].admitted_now = true;
    for (int an = 0; an < hand_expected_value[i].win_vector.size(); an++) {
      if (decline_win_flags[hand_expected_value[i].win_vector[an].tile]) {
        hand_expected_value[i].admitted_now = false;
      }
    }
    hand_expected_value[i].prob_now =
        hand_expected_value[i].admitted_now ? hand_expected_value[i].prob : 0.0F;
  }
  {
    // The items other than tenpai_prob_est can be defined separately. This is postponed because it
    // is complicated.
    std::array<float, 3> flushing_prob, flushing_tenpai_prob;
    for (Color_Type color = CT_CHARACTERS; color < CT_HONOR_TILE; ++color) {
      flushing_prob[color] = cal_flushing_prob(game_state, target, color);
      flushing_tenpai_prob[color] =
          flushing_prob[color] * cal_flushing_tenpai_post(game_state, target, color);
    }
    const float normal_tenpai_prob =
        (1.0 - flushing_prob[CT_CHARACTERS] - flushing_prob[CT_DOTS] - flushing_prob[CT_BAMBOO]) *
        infer_tenpai_prob(game_record, game_state, target);
    const float tenpai_prob = normal_tenpai_prob + flushing_tenpai_prob[CT_CHARACTERS] +
                              flushing_tenpai_prob[CT_DOTS] + flushing_tenpai_prob[CT_BAMBOO];
    normalize2(game_state.player_state[target].open_meld, tenpai_prob, normal_tenpai_prob,
               flushing_tenpai_prob, hand_expected_value);
  }
  return hand_expected_value;
}

void Hand_Estimator::normalize2(const Open_Meld_Vector &open_meld, const float tenpai_prob,
                                const float normal_tenpai_prob,
                                const std::array<float, 3> flushing_tenpai_prob,
                                std::vector<Hand_Estimator_Element> &hand_expected_value) {
  unsigned long long den_normal = 0;
  unsigned long long den_normal_now = 0;
  unsigned long long den_flushing[3] = {};
  unsigned long long den_flushing_now[3] = {};
  for (int i = 0; i < hand_expected_value.size(); i++) {
    const Color_Type flushing_color = hand_expected_value[i].get_flushing_color(open_meld);
    if (flushing_color == CT_NUM) {
      den_normal += hand_expected_value[i].weight_units;
      if (hand_expected_value[i].admitted_now)
        den_normal_now += hand_expected_value[i].weight_units;
    } else {
      den_flushing[flushing_color] += hand_expected_value[i].weight_units;
      if (hand_expected_value[i].admitted_now)
        den_flushing_now[flushing_color] += hand_expected_value[i].weight_units;
    }
  }

  for (int i = 0; i < hand_expected_value.size(); i++) {
    const Color_Type flushing_color = hand_expected_value[i].get_flushing_color(open_meld);
    if (flushing_color == CT_NUM) {
      if (den_normal > 0) {
        hand_expected_value[i].prob = static_cast<float>(hand_expected_value[i].weight_units) /
                                      static_cast<float>(den_normal) * normal_tenpai_prob /
                                      tenpai_prob;
      }
      if (den_normal_now > 0 && hand_expected_value[i].admitted_now) {
        hand_expected_value[i].prob_now = static_cast<float>(hand_expected_value[i].weight_units) /
                                          static_cast<float>(den_normal_now) * normal_tenpai_prob /
                                          tenpai_prob;
      } else {
        hand_expected_value[i].prob_now = 0.0F;
      }
    } else {
      if (den_flushing[flushing_color] > 0) {
        hand_expected_value[i].prob = static_cast<float>(hand_expected_value[i].weight_units) /
                                      static_cast<float>(den_flushing[flushing_color]) *
                                      flushing_tenpai_prob[flushing_color] / tenpai_prob;
      }
      if (den_flushing_now[flushing_color] > 0 && hand_expected_value[i].admitted_now) {
        hand_expected_value[i].prob_now = static_cast<float>(hand_expected_value[i].weight_units) /
                                          static_cast<float>(den_flushing_now[flushing_color]) *
                                          flushing_tenpai_prob[flushing_color] / tenpai_prob;
      } else {
        hand_expected_value[i].prob_now = 0.0F;
      }
    }
  }
}

std::array<std::array<std::array<float, 12>, 14>, 38> cal_tile_prob_from_hand_expected_value(
    const std::vector<Hand_Estimator_Element> &hand_expected_value, const bool is_tsumo,
    const bool is_now) {
  std::array<std::array<std::array<float, 12>, 14>, 38> tile_prob;
  for (int tile = 0; tile < 38; tile++) {
    for (int han = 0; han < 14; han++) {
      for (int fu = 0; fu < 12; fu++) {
        tile_prob[tile][han][fu] = 0.0;
      }
    }
  }
  for (int i = 0; i < hand_expected_value.size(); i++) {
    for (int an = 0; an < hand_expected_value[i].win_vector.size(); an++) {
      const int han = hand_expected_value[i].win_vector[an].han;
      if (han > 0) {
        int fu;
        if (is_tsumo) {
          fu = (hand_expected_value[i].win_vector[an].fu + 2 + 9) / 10;
        } else {
          fu = (hand_expected_value[i].win_vector[an].fu + 9) / 10;
          if (fu == 2) {
            fu = 3;
          }
        }
        if (is_now) {
          tile_prob[hand_expected_value[i].win_vector[an].tile][han][fu] +=
              hand_expected_value[i].prob_now;
        } else {
          tile_prob[hand_expected_value[i].win_vector[an].tile][han][fu] +=
              hand_expected_value[i].prob;
        }
      }
    }
  }
  if (!is_tsumo) {
    for (int c = 0; c < 3; c++) {
      for (int han = 0; han < 14; han++) {
        for (int fu = 0; fu < 12; fu++) {
          tile_prob[10 * c + 10][std::min(han + 1, 13)][fu] += tile_prob[10 * c + 5][han][fu];
        }
      }
    }
  }
  return tile_prob;
}

Wait_Coeff::Wait_Coeff() {}

void Wait_Coeff::init_coeff(const int my_pid) {
  {
    for (int i = 0; i < 4; i++) {
      shape_prob[i] = 0.0;
    }
    for (int tile = 0; tile < 38; tile++) {
      if (tile % 10 != 0) {
        pair_wait_coeff[tile] = 1.0;
      } else {
        pair_wait_coeff[tile] = 0.0;
        dual_pair_coeff[tile] = 0.0;
      }
    }
    dual_pair_coeff[31] = 0.242 / 4.0;
    dual_pair_coeff[32] = 0.242 / 4.0;
    dual_pair_coeff[33] = 0.242 / 4.0;
    dual_pair_coeff[34] = 0.242 / 4.0;
    dual_pair_coeff[35] = 0.206 / 3.0;
    dual_pair_coeff[36] = 0.206 / 3.0;
    dual_pair_coeff[37] = 0.206 / 3.0;

    for (int j = 0; j < 3; j++) {
      open_wait_coeff[j][0] = 0.0;
      open_wait_coeff[j][1] = 0.168 / 3.0;
      open_wait_coeff[j][2] = 0.181 / 3.0;
      open_wait_coeff[j][3] = 0.196 / 3.0;
      open_wait_coeff[j][4] = 0.196 / 3.0;
      open_wait_coeff[j][5] = 0.181 / 3.0;
      open_wait_coeff[j][6] = 0.168 / 3.0;
      dual_pair_coeff[10 * j + 0] = 0.0;
      dual_pair_coeff[10 * j + 1] = 0.194 / 3.0;
      dual_pair_coeff[10 * j + 2] = 0.184 / 3.0;
      dual_pair_coeff[10 * j + 3] = 0.152 / 3.0;
      dual_pair_coeff[10 * j + 4] = 0.155 / 3.0;
      dual_pair_coeff[10 * j + 5] = 0.184 / 3.0;
      dual_pair_coeff[10 * j + 6] = 0.155 / 3.0;
      dual_pair_coeff[10 * j + 7] = 0.152 / 3.0;
      dual_pair_coeff[10 * j + 8] = 0.184 / 3.0;
      dual_pair_coeff[10 * j + 9] = 0.194 / 3.0;
      kanchan_coeff[j][0] = 0.0;
      kanchan_coeff[j][1] = 0.0;
      kanchan_coeff[j][2] = 0.12 / 3.0;
      kanchan_coeff[j][3] = 0.13 / 3.0;
      kanchan_coeff[j][4] = 0.10 / 3.0;
      kanchan_coeff[j][5] = 0.09 / 3.0;
      kanchan_coeff[j][6] = 0.10 / 3.0;
      kanchan_coeff[j][7] = 0.13 / 3.0;
      kanchan_coeff[j][8] = 0.12 / 3.0;
      edge_wait_coeff[j][0] = 0.105 / 3.0;
      edge_wait_coeff[j][1] = 0.105 / 3.0;
    }
  }
}

void Wait_Coeff::set_shape_prob(const int my_pid) {
  {
    shape_prob[0] = 0.6;
    shape_prob[1] = 0.122;
    shape_prob[2] = 0.08;
    shape_prob[3] = 0.243;
    float total = shape_prob[0] + shape_prob[1] + shape_prob[2] + shape_prob[3];
    float correction[19];
    correction[18] = 1.0;
    correction[17] = 0.99;
    correction[16] = 0.98;
    correction[15] = 0.97;
    correction[14] = 0.96;
    correction[13] = 0.95;
    correction[12] = 0.94;
    correction[11] = 0.93;
    correction[10] = 0.92;
    correction[9] = 0.91;
    correction[8] = 0.90;
    correction[7] = 0.89;
    correction[6] = 0.88;
    correction[5] = 0.85;
    correction[4] = 0.78;
    correction[3] = 0.68;
    correction[2] = 0.55;
    correction[1] = 0.35;
    correction[0] = 0.0;

    int remaining = 18;
    for (int i = 0; i < 3; i++) {
      for (int j = 1; j <= 6; j++) {
        if (open_wait_coeff[i][j] == 0.0) {
          remaining--;
        }
      }
    }
    float tmp;
    tmp = (total - shape_prob[0] * correction[remaining]) / (total - shape_prob[0]);
    shape_prob[0] = shape_prob[0] * correction[remaining];
    shape_prob[1] = shape_prob[1] * tmp;
    shape_prob[2] = shape_prob[2] * tmp;
    shape_prob[3] = shape_prob[3] * tmp;
  }
}

void Wait_Coeff::safe_flag_to_coeff(const std::array<bool, 38> &safe) {
  for (int tile = 1; tile < 38; tile++) {
    if (safe[tile]) {
      pair_wait_coeff[tile] = 0;
      dual_pair_coeff[tile] = 0;
    }
  }
  for (int j = 0; j < 3; j++) {
    for (int i = 1; i <= 6; i++) {
      if (safe[10 * j + i] || safe[10 * j + i + 3]) {
        open_wait_coeff[j][i] = 0;
      }
    }
    for (int i = 2; i <= 8; i++) {
      if (safe[10 * j + i]) {
        kanchan_coeff[j][i] = 0;
      }
    }
    if (safe[10 * j + 3]) {
      edge_wait_coeff[j][0] = 0;
    }
    if (safe[10 * j + 7]) {
      edge_wait_coeff[j][1] = 0;
    }
  }
}

void Wait_Coeff::visible_to_coeff(const Tile_Array &visible_all, const Tile_Array &visible) {
  for (int tile = 0; tile < 38; tile++) {
    if (visible_all[tile] >= 2) {
      dual_pair_coeff[tile] = 0;
    }
    if (visible_all[tile] >= 3) {
      pair_wait_coeff[tile] = 0;
    }
  }

  for (int tile = 0; tile < 38; tile++) {
    if (visible[tile] >= 3) {
      dual_pair_coeff[tile] = 0;
    }
    if (visible[tile] >= 4) {
      pair_wait_coeff[tile] = 0;
    }
  }

  for (int j = 0; j < 3; j++) {
    for (int i = 1; i <= 6; i++) {
      if (visible[j * 10 + i + 1] == 4 || visible[j * 10 + i + 2] == 4) {
        open_wait_coeff[j][i] = 0;
      } else if (visible[j * 10 + i + 1] == 3 || visible[j * 10 + i + 2] == 3) {
        open_wait_coeff[j][i] *= 0.5;
      } else if (visible[j * 10 + i + 1] == 2 || visible[j * 10 + i + 2] == 2) {
        open_wait_coeff[j][i] *= 0.8;
      }
    }
    for (int i = 2; i <= 8; i++) {
      if (visible[j * 10 + i - 1] == 4 || visible[j * 10 + i + 1] == 4) {
        kanchan_coeff[j][i] = 0;
      } else if (visible[j * 10 + i - 1] == 3 || visible[j * 10 + i + 1] == 3) {
        kanchan_coeff[j][i] *= 0.5;
      } else if (visible[j * 10 + i - 1] == 2 || visible[j * 10 + i + 1] == 2) {
        kanchan_coeff[j][i] *= 0.8;
      }
    }
    if (visible[j * 10 + 1] == 4 || visible[j * 10 + 2] == 4) {
      edge_wait_coeff[j][0] = 0;
    } else if (visible[j * 10 + 1] == 3 || visible[j * 10 + 2] == 3) {
      edge_wait_coeff[j][0] *= 0.5;
    } else if (visible[j * 10 + 1] == 2 || visible[j * 10 + 2] == 2) {
      edge_wait_coeff[j][0] *= 0.8;
    }
    if (visible[j * 10 + 8] == 4 || visible[j * 10 + 9] == 4) {
      edge_wait_coeff[j][1] = 0;
    } else if (visible[j * 10 + 8] == 3 || visible[j * 10 + 9] == 3) {
      edge_wait_coeff[j][1] *= 0.5;
    } else if (visible[j * 10 + 8] == 2 || visible[j * 10 + 9] == 2) {
      edge_wait_coeff[j][1] *= 0.8;
    }
  }
}

void Wait_Coeff::discard_before_riichi_to_coeff(const Game_State &game_state, const int my_pid,
                                                const int target) {
  {
    const std::array<bool, 38> discard_before_riichi =
        get_discard_before_riichi(game_state.player_state[target].river);
    for (int j = 0; j < 3; j++) {
      if (discard_before_riichi[10 * j + 2]) {
        dual_pair_coeff[10 * j + 3] *= 0.419;
      }
      if (discard_before_riichi[10 * j + 3]) {
        dual_pair_coeff[10 * j + 2] *= 0.335;
        dual_pair_coeff[10 * j + 4] *= 0.387;
      }
      if (discard_before_riichi[10 * j + 4]) {
        dual_pair_coeff[10 * j + 3] *= 0.360;
        dual_pair_coeff[10 * j + 5] *= 0.282;
      }
      if (discard_before_riichi[10 * j + 5]) {
        dual_pair_coeff[10 * j + 4] *= 0.338;
        dual_pair_coeff[10 * j + 6] *= 0.338;
      }
      if (discard_before_riichi[10 * j + 6]) {
        dual_pair_coeff[10 * j + 5] *= 0.282;
        dual_pair_coeff[10 * j + 7] *= 0.360;
      }
      if (discard_before_riichi[10 * j + 7]) {
        dual_pair_coeff[10 * j + 6] *= 0.387;
        dual_pair_coeff[10 * j + 8] *= 0.335;
      }
      if (discard_before_riichi[10 * j + 8]) {
        dual_pair_coeff[10 * j + 7] *= 0.419;
      }

      if (discard_before_riichi[10 * j + 2]) {
        kanchan_coeff[j][4] *= 0.574;
        kanchan_coeff[j][5] *= 1.05;
        kanchan_coeff[j][6] *= 1.18;
        kanchan_coeff[j][7] *= 1.00;
        kanchan_coeff[j][8] *= 1.10;
      }
      if (discard_before_riichi[10 * j + 3]) {
        kanchan_coeff[j][5] *= 0.397;
        kanchan_coeff[j][6] *= 1.28;
        kanchan_coeff[j][7] *= 1.16;
        kanchan_coeff[j][8] *= 1.01;
      }
      if (discard_before_riichi[10 * j + 4]) {
        kanchan_coeff[j][2] *= 0.141;
        kanchan_coeff[j][6] *= 0.249;
        kanchan_coeff[j][7] *= 1.66;
        kanchan_coeff[j][8] *= 1.14;
      }
      if (discard_before_riichi[10 * j + 5]) {
        kanchan_coeff[j][2] *= 1.57;
        kanchan_coeff[j][3] *= 0.139;
        kanchan_coeff[j][7] *= 0.139;
        kanchan_coeff[j][8] *= 1.57;
      }
      if (discard_before_riichi[10 * j + 6]) {
        kanchan_coeff[j][2] *= 1.14;
        kanchan_coeff[j][3] *= 1.66;
        kanchan_coeff[j][4] *= 0.249;
        kanchan_coeff[j][8] *= 0.141;
      }
      if (discard_before_riichi[10 * j + 7]) {
        kanchan_coeff[j][2] *= 1.01;
        kanchan_coeff[j][3] *= 1.16;
        kanchan_coeff[j][4] *= 1.28;
        kanchan_coeff[j][5] *= 0.397;
      }
      if (discard_before_riichi[10 * j + 8]) {
        kanchan_coeff[j][2] *= 1.10;
        kanchan_coeff[j][3] *= 1.00;
        kanchan_coeff[j][4] *= 1.18;
        kanchan_coeff[j][5] *= 1.05;
        kanchan_coeff[j][6] *= 0.574;
      }
      if (discard_before_riichi[10 * j + 10]) {
        open_wait_coeff[j][3] *= 0.1;
        open_wait_coeff[j][4] *= 0.1;
        kanchan_coeff[j][4] *= 0.1;
        kanchan_coeff[j][6] *= 0.1;
      }
    }
  }
}

void Wait_Coeff::ratio_proto_sequence_to_coeff(const Game_State &game_state, const int my_pid,
                                               const int target) {
  {
    int widx, discard[38];  // discard_kind[6] is unused now. Verify what it was for.
    float init;

    float wr[3][7];
    wr[0][0] = -2.48736943975;
    wr[0][1] = -0.38358839454;
    wr[0][2] = -0.234425064762;
    wr[0][3] = -0.288325697454;
    wr[0][4] = -0.280881687069;
    wr[0][5] = -0.351783126768;
    wr[0][6] = -0.279311251629;
    wr[1][0] = -2.55624457004;
    wr[1][1] = -0.195951464576;
    wr[1][2] = -0.148550415776;
    wr[1][3] = -0.208170579705;
    wr[1][4] = -0.137090025131;
    wr[1][5] = -0.187566539347;
    wr[1][6] = -0.194074003517;
    wr[2][0] = -2.6184338081;
    wr[2][1] = -0.151380323926;
    wr[2][2] = -0.00916219707065;
    wr[2][3] = -0.0797609698755;
    wr[2][4] = -0.0990116476359;
    wr[2][5] = -0.00530004986828;
    wr[2][6] = 0.0775349456586;

    const std::array<std::array<std::array<bool, 38>, 7>, 3> proto_sequence_open_wait =
        get_proto_sequence_open_wait(game_state, target);
    for (int j = 0; j < 3; j++) {
      for (int i = 1; i <= 6; i++) {
        if (proto_sequence_open_wait[j][i][0]) {
          for (int tile = 1; tile < 38; tile++) {
            discard[tile] = proto_sequence_open_wait[j][i][tile] ? 1 : 0;
          }
          float x[7];
          x[0] = 1.0;
          x[1] = discard[31] + discard[32] + discard[33] + discard[34] + discard[35] + discard[36] +
                 discard[37];
          x[2] = discard[1] + discard[9] + discard[11] + discard[19] + discard[21] + discard[29];
          x[3] = discard[2] + discard[8] + discard[12] + discard[18] + discard[22] + discard[28];
          x[4] = discard[3] + discard[7] + discard[13] + discard[17] + discard[23] + discard[27];
          x[5] = discard[4] + discard[6] + discard[14] + discard[16] + discard[24] + discard[26];
          x[6] = discard[5] + discard[15] + discard[25];
          if (i == 1 || i == 6) {
            widx = 0;
            init = 0.168 / 3.0;
          } else if (i == 2 || i == 5) {
            widx = 1;
            init = 0.181 / 3.0;
          } else {
            widx = 2;
            init = 0.196 / 3.0;
          }

          open_wait_coeff[j][i] *= logistic(wr[widx], x, 7) / init;
        }
      }
    }

    float wk[4][7];
    wk[0][0] = -3.07019308916;
    wk[0][1] = -0.809086145736;
    wk[0][2] = -0.490092420912;
    wk[0][3] = -0.762531563447;
    wk[0][4] = -0.623096181354;
    wk[0][5] = -0.931984101813;
    wk[0][6] = -0.262703087174;
    wk[1][0] = -3.53708536445;
    wk[1][1] = -0.397471800839;
    wk[1][2] = -0.489336312143;
    wk[1][3] = -0.486722020237;
    wk[1][4] = -0.543432338931;
    wk[1][5] = -0.104184045047;
    wk[1][6] = -0.424668245222;
    wk[2][0] = -4.04134667107;
    wk[2][1] = -0.618952951894;
    wk[2][2] = -0.017984967262;
    wk[2][3] = -0.263343090477;
    wk[2][4] = 0.111704196848;
    wk[2][5] = -0.687961773374;
    wk[2][6] = -0.696477050202;
    wk[3][0] = -3.93164365221;
    wk[3][1] = -2.94236400573;
    wk[3][2] = -0.750416658748;
    wk[3][3] = 0.509000083432;
    wk[3][4] = -0.598111084665;
    wk[3][5] = -1.30118462769;
    wk[3][6] = -0.690204046642;

    const std::array<std::array<std::array<bool, 38>, 9>, 3> proto_sequence_kanchan =
        get_proto_sequence_kanchan(game_state, target);
    for (int j = 0; j < 3; j++) {
      for (int i = 2; i <= 8; i++) {
        if (proto_sequence_kanchan[j][i][0]) {
          for (int tile = 1; tile < 38; tile++) {
            discard[tile] = proto_sequence_kanchan[j][i][tile] ? 1 : 0;
          }
          float x[7];
          x[0] = 1.0;
          x[1] = discard[31] + discard[32] + discard[33] + discard[34] + discard[35] + discard[36] +
                 discard[37];
          x[2] = discard[1] + discard[9] + discard[11] + discard[19] + discard[21] + discard[29];
          x[3] = discard[2] + discard[8] + discard[12] + discard[18] + discard[22] + discard[28];
          x[4] = discard[3] + discard[7] + discard[13] + discard[17] + discard[23] + discard[27];
          x[5] = discard[4] + discard[6] + discard[14] + discard[16] + discard[24] + discard[26];
          x[6] = discard[5] + discard[15] + discard[25];
          if (i == 2 || i == 8) {
            widx = 0;
            init = 0.12 / 3.0;
          } else if (i == 3 || i == 7) {
            widx = 1;
            init = 0.13 / 3.0;
          } else if (i == 4 || i == 6) {
            widx = 2;
            init = 0.10 / 3.0;
          } else {
            widx = 3;
            init = 0.09 / 3.0;
          }

          kanchan_coeff[j][i] *= logistic(wk[widx], x, 7) / init;
        }
      }
    }

    float wp[7];
    wp[0] = -3.5286794012;
    wp[1] = -0.453487059423;
    wp[2] = -0.186048267121;
    wp[3] = -0.600585553325;
    wp[4] = -0.558353762301;
    wp[5] = -0.704824584067;
    wp[6] = -0.839010788576;

    const std::array<std::array<std::array<bool, 38>, 2>, 3> proto_sequence_edge_wait =
        get_proto_sequence_edge_wait(game_state, target);
    for (int j = 0; j < 3; j++) {
      for (int i = 0; i < 2; i++) {
        if (proto_sequence_edge_wait[j][i][0]) {
          for (int tile = 1; tile < 38; tile++) {
            discard[tile] = proto_sequence_edge_wait[j][i][tile] ? 1 : 0;
          }
          float x[7];
          x[0] = 1.0;
          x[1] = discard[31] + discard[32] + discard[33] + discard[34] + discard[35] + discard[36] +
                 discard[37];
          x[2] = discard[1] + discard[9] + discard[11] + discard[19] + discard[21] + discard[29];
          x[3] = discard[2] + discard[8] + discard[12] + discard[18] + discard[22] + discard[28];
          x[4] = discard[3] + discard[7] + discard[13] + discard[17] + discard[23] + discard[27];
          x[5] = discard[4] + discard[6] + discard[14] + discard[16] + discard[24] + discard[26];
          x[6] = discard[5] + discard[15] + discard[25];
          init = 0.105 / 3.0;

          edge_wait_coeff[j][i] *= logistic(wp, x, 7) / init;
        }
      }
    }
  }
}

void Wait_Coeff::std_coeff() {
  double sum[4];
  for (int i = 0; i < 4; i++) {
    sum[i] = 0.0;
  }
  for (int tile = 0; tile < 38; tile++) {
    sum[1] = sum[1] + dual_pair_coeff[tile];
    sum[2] = sum[2] + pair_wait_coeff[tile];
  }
  for (int tile = 0; tile < 38; tile++) {
    if (sum[1] > 0.0) {
      dual_pair_coeff[tile] = 2.0 * (dual_pair_coeff[tile]) / sum[1];
    }
    if (sum[2] > 0.0) {
      pair_wait_coeff[tile] = pair_wait_coeff[tile] / sum[2];
    }
  }

  for (int j = 0; j < 3; j++) {
    for (int i = 1; i <= 6; i++) {
      sum[0] = sum[0] + open_wait_coeff[j][i];
    }
    for (int i = 2; i <= 8; i++) {
      sum[3] = sum[3] + kanchan_coeff[j][i];
    }
    sum[3] = sum[3] + edge_wait_coeff[j][0];
    sum[3] = sum[3] + edge_wait_coeff[j][1];
  }

  for (int j = 0; j < 3; j++) {
    if (sum[0] > 0.0) {
      for (int i = 1; i <= 6; i++) {
        open_wait_coeff[j][i] = open_wait_coeff[j][i] / sum[0];
      }
    }
    if (sum[3] > 0.0) {
      for (int i = 2; i <= 8; i++) {
        kanchan_coeff[j][i] = kanchan_coeff[j][i] / sum[3];
      }
      edge_wait_coeff[j][0] = edge_wait_coeff[j][0] / sum[3];
      edge_wait_coeff[j][1] = edge_wait_coeff[j][1] / sum[3];
    }
  }
}

std::array<std::array<std::array<float, 12>, 14>, 38> cal_tile_prob_from_wait_coeff(
    const Game_State &game_state, const Wait_Coeff &wait_coeff,
    const std::array<std::array<double, 12>, 14> &hanfu_weight, const bool is_tsumo) {
  std::array<std::array<std::array<float, 12>, 14>, 38> tile_prob;
  for (int tile = 0; tile < 38; tile++) {
    for (int han = 0; han < 14; han++) {
      for (int fu = 0; fu < 12; fu++) {
        tile_prob[tile][han][fu] = (wait_coeff.shape_prob[1] * wait_coeff.dual_pair_coeff[tile] +
                                    wait_coeff.shape_prob[2] * wait_coeff.pair_wait_coeff[tile]) *
                                   hanfu_weight[han][fu];
      }
    }
  }
  for (int j = 0; j < 3; j++) {
    for (int i = 1; i <= 6; i++) {
      for (int han = 0; han < 14; han++) {
        for (int fu = 0; fu < 12; fu++) {
          tile_prob[j * 10 + i][han][fu] +=
              wait_coeff.shape_prob[0] * wait_coeff.open_wait_coeff[j][i] * hanfu_weight[han][fu];
          tile_prob[j * 10 + i + 3][han][fu] +=
              wait_coeff.shape_prob[0] * wait_coeff.open_wait_coeff[j][i] * hanfu_weight[han][fu];
        }
      }
    }
    for (int i = 2; i <= 8; i++) {
      for (int han = 0; han < 14; han++) {
        for (int fu = 0; fu < 12; fu++) {
          tile_prob[j * 10 + i][han][fu] +=
              wait_coeff.shape_prob[3] * wait_coeff.kanchan_coeff[j][i] * hanfu_weight[han][fu];
        }
      }
    }

    for (int han = 0; han < 14; han++) {
      for (int fu = 0; fu < 12; fu++) {
        tile_prob[j * 10 + 3][han][fu] +=
            wait_coeff.shape_prob[3] * wait_coeff.edge_wait_coeff[j][0] * hanfu_weight[han][fu];
        tile_prob[j * 10 + 7][han][fu] +=
            wait_coeff.shape_prob[3] * wait_coeff.edge_wait_coeff[j][1] * hanfu_weight[han][fu];
      }
    }
  }
  if (!is_tsumo) {
    // The content below comes from cal_tile_prob_riichi_dora_shift().
    std::array<std::array<float, 14>, 38> dora_shift_prob;
    for (int tile = 0; tile < 38; tile++) {
      for (int han = 0; han < 14; han++) {
        dora_shift_prob[tile][han] = 0.0;
      }
    }

    int ds_open_wait[3][7], ds_dual_pair[38], ds_pair_wait[38], ds_kanchan[3][9],
        ds_edge_wait[3][2];
    for (int c = 0; c < 3; c++) {
      for (int i = 1; i <= 6; i++) {
        ds_open_wait[c][i] = tile_dora_han(game_state.dora_marker, c * 10 + i + 1) +
                             tile_dora_han(game_state.dora_marker, c * 10 + i + 2);
      }
      for (int i = 2; i <= 8; i++) {
        ds_kanchan[c][i] = tile_dora_han(game_state.dora_marker, c * 10 + i - 1) +
                           tile_dora_han(game_state.dora_marker, c * 10 + i + 1);
      }
      ds_edge_wait[c][0] = tile_dora_han(game_state.dora_marker, c * 10 + 1) +
                           tile_dora_han(game_state.dora_marker, c * 10 + 2);
      ds_edge_wait[c][1] = tile_dora_han(game_state.dora_marker, c * 10 + 8) +
                           tile_dora_han(game_state.dora_marker, c * 10 + 9);
    }
    for (int tile = 1; tile < 38; tile++) {
      if (tile % 10 != 0) {
        ds_dual_pair[tile] = tile_dora_han(game_state.dora_marker, tile) * 2;
        ds_pair_wait[tile] = tile_dora_han(game_state.dora_marker, tile);
      }
    }

    for (int c = 0; c < 3; c++) {
      for (int i = 1; i <= 6; i++) {
        dora_shift_prob[c * 10 + i][std::min(ds_open_wait[c][i], 13)] +=
            wait_coeff.shape_prob[0] * wait_coeff.open_wait_coeff[c][i];
        dora_shift_prob[c * 10 + i + 3][std::min(ds_open_wait[c][i], 13)] +=
            wait_coeff.shape_prob[0] * wait_coeff.open_wait_coeff[c][i];
      }
      for (int i = 2; i <= 8; i++) {
        dora_shift_prob[c * 10 + i][std::min(ds_kanchan[c][i], 13)] +=
            wait_coeff.shape_prob[3] * wait_coeff.kanchan_coeff[c][i];
      }
      dora_shift_prob[c * 10 + 3][std::min(ds_edge_wait[c][0], 13)] +=
          wait_coeff.shape_prob[3] * wait_coeff.edge_wait_coeff[c][0];
      dora_shift_prob[c * 10 + 7][std::min(ds_edge_wait[c][1], 13)] +=
          wait_coeff.shape_prob[3] * wait_coeff.edge_wait_coeff[c][1];
    }
    for (int tile = 1; tile < 38; tile++) {
      if (tile % 10 != 0) {
        dora_shift_prob[tile][std::min(ds_dual_pair[tile], 13)] +=
            wait_coeff.shape_prob[1] * wait_coeff.dual_pair_coeff[tile];
        dora_shift_prob[tile][std::min(ds_pair_wait[tile], 13)] +=
            wait_coeff.shape_prob[2] * wait_coeff.pair_wait_coeff[tile];
      }
    }

    for (const int dm : game_state.dora_marker) {
      han_prob_shift(dora_shift_prob[dora_marker_to_dora(dm)], 1);
    }
    for (int tile = 0; tile < 38; tile++) {
      if (tile % 10 != 0) {
        float sum = 0.0;
        for (int han = 0; han < 14; han++) {
          for (int fu = 0; fu < 12; fu++) {
            sum += tile_prob[tile][han][fu];
          }
        }
        if (sum > 0.0) {
          for (int han = 0; han < 14; han++) {
            dora_shift_prob[tile][han] = dora_shift_prob[tile][han] / sum;
          }
        }
      }
    }
    for (int tile = 0; tile < 38; tile++) {
      hanfu_prob_han_shift_with_prob(tile_prob[tile], dora_shift_prob[tile]);
    }

    // For a ron win, calculate the red five value and set high values for tiles near the dora. This
    // can apply to a tsumo win too, but it is postponed to match the reference implementation.
    for (int c = 0; c < 3; c++) {
      for (int han = 0; han < 14; han++) {
        for (int fu = 0; fu < 12; fu++) {
          tile_prob[10 * c + 10][std::min(han + 1, 13)][fu] += tile_prob[10 * c + 5][han][fu];
        }
      }
    }
  }
  return tile_prob;
}

std::array<std::array<float, 12>, 14> cal_win_hanfu_prob(
    const std::array<std::array<std::array<float, 12>, 14>, 38> &tile_prob) {
  std::array<std::array<float, 12>, 14> win_prob;
  for (int han = 0; han < 14; han++) {
    for (int fu = 0; fu < 12; fu++) {
      win_prob[han][fu] = 0.0;
    }
  }

  float den = 0.0;
  for (int han = 1; han < 14; han++) {
    for (int fu = 0; fu < 12; fu++) {
      for (int tile = 0; tile < 38; tile++) {
        win_prob[han][fu] += tile_prob[tile][han][fu];
        den += tile_prob[tile][han][fu];
      }
    }
  }
  if (den > 0) {
    for (int han = 1; han < 14; han++) {
      for (int fu = 0; fu < 12; fu++) {
        win_prob[han][fu] = win_prob[han][fu] / den;
      }
    }
  }
  return win_prob;
}

Tenpai_Estimator_Simple::Tenpai_Estimator_Simple() {}

void Tenpai_Estimator_Simple::set_tenpai2(const Moves &game_record, const Game_State &game_state,
                                          const int my_pid, const int target) {
  if (game_state.player_state[target].riichi_declared) {
    tenpai_prob = 1.0;
  } else {
    {
      std::array<float, 3> flushing_prob, flushing_tenpai_prob;
      for (Color_Type color = CT_CHARACTERS; color < CT_HONOR_TILE; ++color) {
        flushing_prob[color] = cal_flushing_prob(game_state, target, color);
        flushing_tenpai_prob[color] =
            flushing_prob[color] * cal_flushing_tenpai_post(game_state, target, color);
      }
      const float normal_tenpai_prob =
          (1.0 - flushing_prob[CT_CHARACTERS] - flushing_prob[CT_DOTS] - flushing_prob[CT_BAMBOO]) *
          infer_tenpai_prob(game_record, game_state, target);
      tenpai_prob = normal_tenpai_prob + flushing_tenpai_prob[CT_CHARACTERS] +
                    flushing_tenpai_prob[CT_DOTS] + flushing_tenpai_prob[CT_BAMBOO];
    }
  }
}

void Tenpai_Estimator_Simple::set_tenpai_estimator(const Moves &game_record,
                                                   const Game_State &game_state, const int my_pid,
                                                   const int target, const Tactics &tactics) {
  const Tile_Array visible_all = get_tile_visible_all(game_state);
  const Tile_Array visible_all_kind = tile_kind(visible_all);
  const Tile_Array hand_kind = tile_kind(game_state.player_state[my_pid].hand);
  const Tile_Array visible_kind =
      (target == my_pid) ? visible_all_kind : sum_tile_array(visible_all_kind, hand_kind);
  const Tile_Array remaining_kind = cal_remaining_kind_array(visible_kind);
  const std::array<bool, 38> discard_kind = get_discard_kind(game_state.player_state[target].river);
  const std::array<bool, 38> decline_win_ar = get_decline_win_ar(game_record, game_state, target);
  const std::array<bool, 38> is_furiten = get_furiten_flags(game_record, game_state, target, false);

  // When the player has open melds, set skip_latest of get_furiten_flags to false. The evaluation
  // assumes that other players declined the win.

  set_tenpai2(game_record, game_state, my_pid, target);
  if (game_state.player_state[target].riichi_declared ||
      game_state.player_state[target].open_meld.size() == 0) {
    Wait_Coeff wait_coeff;
    wait_coeff.init_coeff(my_pid);
    wait_coeff.safe_flag_to_coeff(discard_kind);
    wait_coeff.safe_flag_to_coeff(decline_win_ar);
    wait_coeff.visible_to_coeff(visible_all_kind, visible_kind);
    wait_coeff.set_shape_prob(my_pid);
    wait_coeff.ratio_proto_sequence_to_coeff(game_state, my_pid, target);
    wait_coeff.discard_before_riichi_to_coeff(game_state, my_pid, target);
    Wait_Coeff wait_coeff_now = wait_coeff;
    wait_coeff_now.safe_flag_to_coeff(is_furiten);

    wait_coeff.std_coeff();
    wait_coeff_now.std_coeff();

    // TODO: Move this processing into Wait_Coeff and support multiple discard flags.
    if (console_out) {
      std::cout << "shape_prob" << std::endl;
      std::cout << wait_coeff.shape_prob[0] << " " << wait_coeff.shape_prob[1] << " "
                << wait_coeff.shape_prob[2] << " " << wait_coeff.shape_prob[3] << std::endl;
    }

    tile_ron_prob =
        cal_tile_prob_from_wait_coeff(game_state, wait_coeff, tactics.hanfu_weight_ron, false);
    tile_tsumo_prob =
        cal_tile_prob_from_wait_coeff(game_state, wait_coeff, tactics.hanfu_weight_tsumo, true);
    tile_ron_prob_now =
        cal_tile_prob_from_wait_coeff(game_state, wait_coeff_now, tactics.hanfu_weight_ron, false);

    if (game_state.player_state[target].riichi_declared && is_ippatsu_valid(game_record, target)) {
      for (int tile = 1; tile < 38; tile++) {
        hanfu_prob_han_shift(tile_ron_prob_now[tile], 1);
      }
    }
  } else {
    Hand_Estimator hand_estimator;
    const std::vector<Hand_Estimator_Element> hand_expected_value =
        hand_estimator.cal_hand_expected_value_with_prob(
            game_record, game_state, target,
            std::min(7, 13 - (int)game_state.player_state[target].open_meld.size() * 3),
            remaining_kind, discard_kind);
    tile_tsumo_prob = cal_tile_prob_from_hand_expected_value(hand_expected_value, true, false);
    tile_ron_prob = cal_tile_prob_from_hand_expected_value(hand_expected_value, false, false);
    tile_ron_prob_now = cal_tile_prob_from_hand_expected_value(hand_expected_value, false, true);
  }
}

std::array<float, 4> get_tenpai_prob_array(
    const std::array<Tenpai_Estimator_Simple, 4> &tenpai_estimator) {
  std::array<float, 4> tenpai_prob_array;
  for (int pid = 0; pid < 4; pid++) {
    tenpai_prob_array[pid] = tenpai_estimator[pid].tenpai_prob;
  }
  return tenpai_prob_array;
}

std::array<std::array<std::array<float, 12>, 14>, 4> cal_win_hanfu_prob_array(
    const std::array<Tenpai_Estimator_Simple, 4> &tenpai_estimator, const bool is_tsumo) {
  std::array<std::array<std::array<float, 12>, 14>, 4> win_hanfu_prob_array;
  for (int pid = 0; pid < 4; pid++) {
    const std::array<std::array<std::array<float, 12>, 14>, 38> &tile_prob =
        is_tsumo ? tenpai_estimator[pid].tile_tsumo_prob : tenpai_estimator[pid].tile_ron_prob;
    win_hanfu_prob_array[pid] = cal_win_hanfu_prob(tile_prob);
  }
  return win_hanfu_prob_array;
}

std::pair<std::array<std::array<float, 38>, 4>, std::array<std::array<float, 38>, 4>>
cal_deal_in_tile_prob_value(
    const std::array<Tenpai_Estimator_Simple, 4> &tenpai_estimator,
    const std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> &round_end_pt_exp,
    const int my_pid, const bool is_now) {
  std::array<std::array<float, 38>, 4> deal_in_tile_prob, deal_in_tile_value;
  for (int pid = 0; pid < 4; pid++) {
    const std::array<std::array<std::array<float, 12>, 14>, 38> &deal_in_tile_hanfu_prob =
        is_now ? tenpai_estimator[pid].tile_ron_prob_now : tenpai_estimator[pid].tile_ron_prob;
    for (int tile = 0; tile < 38; tile++) {
      deal_in_tile_prob[pid][tile] = 0.0;
      deal_in_tile_value[pid][tile] = 0.0;
      for (int han = 0; han < 14; han++) {
        for (int fu = 0; fu < 12; fu++) {
          deal_in_tile_prob[pid][tile] += deal_in_tile_hanfu_prob[tile][han][fu];
          deal_in_tile_value[pid][tile] +=
              deal_in_tile_hanfu_prob[tile][han][fu] * round_end_pt_exp[pid][my_pid][han][fu];
        }
      }
      if (deal_in_tile_prob[pid][tile] > 0.0) {
        deal_in_tile_value[pid][tile] =
            deal_in_tile_value[pid][tile] / deal_in_tile_prob[pid][tile];
      }
    }
  }
  return std::pair<std::array<std::array<float, 38>, 4>, std::array<std::array<float, 38>, 4>>(
      deal_in_tile_prob, deal_in_tile_value);
}

TotalDealIn cal_total_deal_in_tile_prob_value(
    const int my_pid, const std::array<Tenpai_Estimator_Simple, 4> &tenpai_estimator,
    const std::array<std::array<float, 38>, 4> &deal_in_tile_prob,
    const std::array<std::array<float, 38>, 4> &deal_in_tile_value) {
  std::array<float, 38> total_deal_in_prob, total_deal_in_value;
  std::array<float, 38> weighted_utility;
  for (int tile = 0; tile < 38; tile++) {
    total_deal_in_prob[tile] = 0.0;
    total_deal_in_value[tile] = 0.0;
    for (int pid = 0; pid < 4; pid++) {
      if (pid != my_pid) {
        total_deal_in_prob[tile] +=
            tenpai_estimator[pid].tenpai_prob * deal_in_tile_prob[pid][tile];
        total_deal_in_value[tile] += tenpai_estimator[pid].tenpai_prob *
                                     deal_in_tile_prob[pid][tile] * deal_in_tile_value[pid][tile];
      }
      if (console_out) {
        std::cout << deal_in_tile_prob[pid][tile] << " " << deal_in_tile_value[pid][tile] << " ";
      }
    }
    if (console_out) {
      std::cout << std::endl;
    }
    weighted_utility[tile] = total_deal_in_value[tile];
    if (total_deal_in_prob[tile] > 0.0) {
      total_deal_in_value[tile] = total_deal_in_value[tile] / total_deal_in_prob[tile];
      total_deal_in_prob[tile] = std::min(total_deal_in_prob[tile], (float)1.0);
    }
  }
  return {total_deal_in_prob, total_deal_in_value, weighted_utility};
}
