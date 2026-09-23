#include "calc_yaku.hpp"

bool closed_hand_check(const Open_Meld_Vector &open_meld) {
  for (int i = 0; i < open_meld.size(); i++) {
    if (open_meld[i].type != FT_CONCEALED_KAN) {
      return false;
    }
  }
  return true;
}

bool value_tile_check(const int value_tile, const Tile_Array &hand_rest,
                      const Open_Meld_Vector &open_meld, const int wait_tile,
                      const Wait_Type wait_type) {
  for (int i = 0; i < open_meld.size(); i++) {
    if (open_meld[i].consumed[0] == value_tile) {
      return true;
    }
  }

  if (hand_rest[value_tile] == 3) {
    return true;
  } else if (wait_type == MT_DUAL_PAIR && wait_tile == value_tile) {
    return true;
  } else {
    return false;
  }
}

bool pinfu_check(const Tile_Array &hand_rest, const Open_Meld_Vector &open_meld,
                 const Wait_Type wait_type, const int round_wind_tile, const int self_wind_tile) {
  if (0 < open_meld.size()) {
    return false;
  }
  if (wait_type != MT_OPEN_WAIT) {
    return false;
  }
  for (int tile = 1; tile < 38; tile++) {
    if (hand_rest[tile] == 3) {
      return false;
    }
  }
  if (hand_rest[round_wind_tile] == 2 || hand_rest[self_wind_tile] == 2) {
    return false;
  }
  if (hand_rest[35] == 2 || hand_rest[36] == 2 || hand_rest[37] == 2) {
    return false;
  }
  return true;
}

bool full_flush_check(const Tile_Array &hand, const Open_Meld_Vector &open_meld) {
  bool flags[3];
  for (int i = 0; i < 3; i++) {
    flags[i] = true;
  }
  for (Color_Type color = (Color_Type)0; color < CT_HONOR_TILE; ++color) {
    for (int fn = 0; fn < open_meld.size(); fn++) {
      if (tile_color(open_meld[fn].consumed[0]) != color) {
        flags[color] = false;
        break;
      }
    }
    if (flags[color]) {
      for (int tile = 0; tile < 38; tile++) {
        if (hand[tile] > 0 && tile_color(tile) != color) {
          flags[color] = false;
          break;
        }
      }
    }
  }
  return flags[0] || flags[1] || flags[2];
}

bool half_flush_check(const Tile_Array &hand, const Open_Meld_Vector &open_meld) {
  bool flags[3];
  for (int i = 0; i < 3; i++) {
    flags[i] = true;
  }
  for (Color_Type color = (Color_Type)0; color < CT_HONOR_TILE; ++color) {
    for (int fn = 0; fn < open_meld.size(); fn++) {
      if (tile_color(open_meld[fn].consumed[0]) != color &&
          tile_color(open_meld[fn].consumed[0]) != CT_HONOR_TILE) {
        flags[color] = false;
        break;
      }
    }
    if (flags[color]) {
      for (int tile = 0; tile < 30; tile++) {
        if (hand[tile] > 0 && tile_color(tile) != color) {
          flags[color] = false;
          break;
        }
      }
    }
  }
  return flags[0] || flags[1] || flags[2];
}

bool simples_check(const Tile_Array &hand, const Open_Meld_Vector &open_meld, const int wait_tile) {
  for (int tile = 0; tile < 38; tile++) {
    if (hand[tile] > 0 && tile_terminal_or_honor(tile) != NT_SIMPLES) {
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
    } else if (tile_terminal_or_honor(open_meld[fn].consumed[0]) != NT_SIMPLES) {
      return false;
    }
  }
  if (tile_terminal_or_honor(wait_tile) != NT_SIMPLES) {
    return false;
  }
  return true;
}

bool terminals_and_honors_check(const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                                const int wait_tile) {
  for (int tile = 0; tile < 38; tile++) {
    if (hand[tile] > 0 && tile_terminal_or_honor(tile) == NT_SIMPLES) {
      return false;
    }
  }
  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].type == FT_CHII) {
      return false;
    } else if (tile_terminal_or_honor(open_meld[fn].consumed[0]) == NT_SIMPLES) {
      return false;
    }
  }
  if (tile_terminal_or_honor(wait_tile) == NT_SIMPLES) {
    return false;
  }
  return true;
}

bool suitsu_check(const Tile_Array &hand, const Open_Meld_Vector &open_meld) {
  for (int tile = 0; tile <= 30; tile++) {
    if (hand[tile] != 0) {
      return false;
    }
  }
  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (tile_color(open_meld[fn].consumed[0]) != CT_HONOR_TILE) {
      return false;
    }
  }
  return true;
}

void add_hand_sequence_to_sequence(Tile_Array &hand_sequence, Tile_Array &sequence) {
  for (int j = 0; j < 3; j++) {
    int ss = 1;
    while (ss <= 7) {
      if (hand_sequence[10 * j + ss] && hand_sequence[10 * j + ss + 1] &&
          hand_sequence[10 * j + ss + 2]) {
        sequence[10 * j + ss]++;
        hand_sequence[10 * j + ss] = hand_sequence[10 * j + ss] - 1;
        hand_sequence[10 * j + ss + 1] = hand_sequence[10 * j + ss + 1] - 1;
        hand_sequence[10 * j + ss + 2] = hand_sequence[10 * j + ss + 2] - 1;
      } else {
        ss = ss + 1;
      }
    }
  }
}

bool pure_outside_hand_check(const Tile_Array &hand, const Tile_Array &hand_rest,
                             const Open_Meld_Vector &open_meld, const Tile_Array &hand_tmp,
                             const int wait_tile) {
  for (int tile = 0; tile < 38; tile++) {
    if (hand_rest[tile] >= 2 && tile_terminal_or_honor(tile) != NT_TERMINAL) {
      return false;
    }
  }

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

  Tile_Array hand_sequence = {};
  Tile_Array sequence = {};
  for (int tile = 0; tile < 30; tile++) {
    hand_sequence[tile] = hand[tile] - hand_rest[tile] - hand_tmp[tile];
    sequence[tile] = 0;
  }
  add_hand_sequence_to_sequence(hand_sequence, sequence);
  for (int j = 0; j < 3; j++) {
    for (int i = 2; i <= 6; i++) {
      if (sequence[10 * j + i] > 0) {
        return false;
      }
    }
  }

  if (tile_terminal_or_honor(wait_tile) == NT_TERMINAL) {
    return true;
  } else {
    for (int tile = 0; tile < 38; tile++) {
      if (hand_tmp[tile] > 0 && tile_terminal_or_honor(tile) == NT_TERMINAL) {
        return true;
      }
    }
  }
  return false;
}

bool outside_hand_check(const Tile_Array &hand, const Tile_Array &hand_rest,
                        const Open_Meld_Vector &open_meld, const Tile_Array &hand_tmp,
                        const int wait_tile) {
  for (int tile = 0; tile < 38; tile++) {
    if (hand_rest[tile] >= 2 && tile_terminal_or_honor(tile) == NT_SIMPLES) {
      return false;
    }
  }

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

  Tile_Array hand_sequence = {};
  Tile_Array sequence = {};
  for (int tile = 0; tile < 30; tile++) {
    hand_sequence[tile] = hand[tile] - hand_rest[tile] - hand_tmp[tile];
    sequence[tile] = 0;
  }
  add_hand_sequence_to_sequence(hand_sequence, sequence);
  for (int j = 0; j < 3; j++) {
    for (int i = 2; i <= 6; i++) {
      if (sequence[10 * j + i] > 0) {
        return false;
      }
    }
  }

  if (tile_terminal_or_honor(wait_tile) != NT_SIMPLES) {
    return true;
  } else {
    for (int tile = 0; tile < 38; tile++) {
      if (hand_tmp[tile] > 0 && tile_terminal_or_honor(tile) != NT_SIMPLES) {
        return true;
      }
    }
  }
  return false;
}

bool all_triplets_check(const Tile_Array &hand_cut, const Open_Meld_Vector &open_meld,
                        const Tile_Array &hand_tmp, const Wait_Type wait_type) {
  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].type == FT_CHII) {
      return false;
    }
  }
  for (int tile = 0; tile < 38; tile++) {
    if (hand_cut[tile] - hand_tmp[tile] > 0) {
      return false;
    }
  }
  if (wait_type == MT_DUAL_PAIR || wait_type == MT_PAIR_WAIT) {
    return true;
  } else {
    return false;
  }
}

int count_pure_double_sequences(const Tile_Array &hand_cut, const Open_Meld_Vector &open_meld,
                                const Tile_Array &hand_tmp, const int wait_tile,
                                const Wait_Type wait_type) {
  if (!closed_hand_check(open_meld)) {
    return false;
  }

  Tile_Array hand_sequence = {};
  Tile_Array sequence = {};
  for (int tile = 0; tile < 30; tile++) {
    hand_sequence[tile] = hand_cut[tile] - hand_tmp[tile];
    sequence[tile] = 0;
  }
  add_hand_sequence_to_sequence(hand_sequence, sequence);
  if (wait_type == MT_OPEN_WAIT || wait_type == MT_KANCHAN || wait_type == MT_EDGE_WAIT) {
    for (int tile = 0; tile < 30; tile++) {
      if (hand_tmp[tile] > 0) {
        if (wait_tile < tile) {
          sequence[wait_tile]++;
        } else {
          sequence[tile]++;
        }
        break;
      }
    }
  }

  int pure_double_sequence_num = 0;
  for (int tile = 0; tile < 30; tile++) {
    pure_double_sequence_num += sequence[tile] / 2;
  }
  return pure_double_sequence_num;
}

bool three_color_triplet_check(const Tile_Array &hand_rest, const Open_Meld_Vector &open_meld,
                               const int wait_tile, const Wait_Type wait_type) {
  Tile_Array triplet = {};

  for (int tile = 0; tile < 30; tile++) {
    if (hand_rest[tile] == 3) {
      triplet[tile]++;
    }
  }

  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].type == FT_PON || open_meld[fn].type == FT_CONCEALED_KAN ||
        open_meld[fn].type == FT_OPEN_KAN || open_meld[fn].type == FT_UPGRADED_KAN) {
      triplet[tile_kind(open_meld[fn].consumed[0])]++;
    }
  }

  if (wait_type == MT_DUAL_PAIR) {
    triplet[wait_tile]++;
  }

  for (int i = 1; i <= 9; i++) {
    if (triplet[i] == 1 && triplet[10 + i] == 1 && triplet[20 + i] == 1) {
      return true;
    }
  }
  return false;
}

bool three_color_sequence_check(const Tile_Array &hand_cut, const Open_Meld_Vector &open_meld,
                                const Tile_Array &hand_tmp, const int wait_tile,
                                const Wait_Type wait_type) {
  Tile_Array hand_sequence = {};
  Tile_Array sequence = {};
  for (int tile = 0; tile < 30; tile++) {
    hand_sequence[tile] = hand_cut[tile] - hand_tmp[tile];
    sequence[tile] = 0;
  }
  add_hand_sequence_to_sequence(hand_sequence, sequence);

  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].type == FT_CHII) {
      sequence[std::min(
          {open_meld[fn].tile, open_meld[fn].consumed[0], open_meld[fn].consumed[1]})]++;
    }
  }

  if (wait_type == MT_OPEN_WAIT || wait_type == MT_KANCHAN || wait_type == MT_EDGE_WAIT) {
    for (int tile = 0; tile < 30; tile++) {
      if (hand_tmp[tile] > 0) {
        if (wait_tile < tile) {
          sequence[wait_tile]++;
        } else {
          sequence[tile]++;
        }
        break;
      }
    }
  }

  for (int i = 1; i <= 7; i++) {
    if (sequence[i] > 0 && sequence[10 + i] > 0 && sequence[20 + i] > 0) {
      return true;
    }
  }
  return false;
}

bool full_straight_check(const Tile_Array &hand_cut, const Open_Meld_Vector &open_meld,
                         const Tile_Array &hand_tmp, const int wait_tile,
                         const Wait_Type wait_type) {
  Tile_Array hand_sequence = {};
  Tile_Array sequence = {};
  for (int tile = 0; tile < 30; tile++) {
    hand_sequence[tile] = hand_cut[tile] - hand_tmp[tile];
    sequence[tile] = 0;
  }
  add_hand_sequence_to_sequence(hand_sequence, sequence);

  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].type == FT_CHII) {
      sequence[std::min(
          {open_meld[fn].tile, open_meld[fn].consumed[0], open_meld[fn].consumed[1]})]++;
    }
  }

  if (wait_type == MT_OPEN_WAIT || wait_type == MT_KANCHAN || wait_type == MT_EDGE_WAIT) {
    for (int tile = 0; tile < 30; tile++) {
      if (hand_tmp[tile] > 0) {
        if (wait_tile < tile) {
          sequence[wait_tile]++;
        } else {
          sequence[tile]++;
        }
        break;
      }
    }
  }

  for (int j = 0; j < 3; j++) {
    if (sequence[j * 10 + 1] > 0 && sequence[j * 10 + 4] > 0 && sequence[j * 10 + 7] > 0) {
      return true;
    }
  }
  return false;
}

bool nine_gates_check(const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                      const int wait_tile) {
  if (open_meld.size() > 0) {
    return false;
  }
  bool flags[3];
  Tile_Array wait_array = {};
  wait_array[wait_tile] = 1;
  for (int color = 0; color < 3; color++) {
    flags[color] = true;
    if (hand[10 * color + 1] + wait_array[10 * color + 1] < 3 ||
        hand[10 * color + 9] + wait_array[10 * color + 9] < 3) {
      flags[color] = false;
      continue;
    }
    for (int i = 2; i <= 8; i++) {
      if (hand[10 * color + i] + wait_array[10 * color + i] == 0) {
        flags[color] = false;
        break;
      }
    }
  }
  return flags[0] || flags[1] || flags[2];
}

int concealed_triplet_num_count(const Tile_Array &hand_rest, const Open_Meld_Vector &open_meld) {
  int concealed_triplet_num = 0;
  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].type == FT_CONCEALED_KAN) {
      concealed_triplet_num++;
    }
  }
  for (int tile = 0; tile < 38; tile++) {
    if (hand_rest[tile] == 3) {
      concealed_triplet_num++;
    }
  }
  return concealed_triplet_num;
}

int kan_num_count(const Open_Meld_Vector &open_meld) {
  int kan_num = 0;
  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].type == FT_CONCEALED_KAN || open_meld[fn].type == FT_OPEN_KAN ||
        open_meld[fn].type == FT_UPGRADED_KAN) {
      kan_num++;
    }
  }
  return kan_num;
}

std::pair<int, int> calc_fu(const int round_wind_tile, const int self_wind_tile,
                            const Tile_Array &hand, const Tile_Array &hand_cut,
                            const Tile_Array &hand_rest, const Tile_Array &hand_tmp,
                            const Open_Meld_Vector &open_meld, const int wait_tile,
                            const Wait_Type wait_type, const bool closed_hand, const bool pinfu,
                            const bool seven_pairs) {
  if (pinfu) {
    return std::pair<int, int>(20, 30);
  }
  if (seven_pairs) {
    return std::pair<int, int>(25, 25);
  }

  int fu_tsumo = 20;
  for (int fn = 0; fn < open_meld.size(); fn++) {
    switch (open_meld[fn].type) {
      case FT_PON:
        fu_tsumo = (tile_terminal_or_honor(open_meld[fn].consumed[0]) == NT_SIMPLES) ? fu_tsumo + 2
                                                                                     : fu_tsumo + 4;
        break;
      case FT_OPEN_KAN:
      case FT_UPGRADED_KAN:
        fu_tsumo = (tile_terminal_or_honor(open_meld[fn].consumed[0]) == NT_SIMPLES)
                       ? fu_tsumo + 8
                       : fu_tsumo + 16;
        break;
      case FT_CONCEALED_KAN:
        fu_tsumo = (tile_terminal_or_honor(open_meld[fn].consumed[0]) == NT_SIMPLES)
                       ? fu_tsumo + 16
                       : fu_tsumo + 32;
        break;
      default:
        break;
    }
  }

  for (int tile = 1; tile < 38; tile++) {
    if (hand_rest[tile] == 3) {
      fu_tsumo = (tile_terminal_or_honor(tile) == NT_SIMPLES) ? fu_tsumo + 4 : fu_tsumo + 8;
    } else if (hand_rest[tile] == 2) {
      if (tile != wait_tile || wait_type != MT_DUAL_PAIR) {
        if (tile >= 35 || tile == round_wind_tile || tile == self_wind_tile) {
          fu_tsumo = fu_tsumo + 2;
        }
      }
    }
  }
  if (wait_type == MT_PAIR_WAIT || wait_type == MT_KANCHAN || wait_type == MT_EDGE_WAIT) {
    fu_tsumo = fu_tsumo + 2;
  }
  if (wait_type == MT_PAIR_WAIT) {
    if (wait_tile >= 35 || wait_tile == round_wind_tile || wait_tile == self_wind_tile) {
      fu_tsumo = fu_tsumo + 2;
    }
  }

  int fu_ron = fu_tsumo;

  fu_tsumo = fu_tsumo + 2;
  if (wait_type == MT_DUAL_PAIR) {
    if (tile_terminal_or_honor(wait_tile) == NT_SIMPLES) {
      fu_tsumo = fu_tsumo + 4;
      fu_ron = fu_ron + 2;
    } else {
      fu_tsumo = fu_tsumo + 8;
      fu_ron = fu_ron + 4;
    }
  }
  if (closed_hand) {
    fu_ron = fu_ron + 10;
  }

  if (fu_tsumo < 30) {
    fu_tsumo = 30;
  }
  if (fu_ron < 30) {
    fu_ron = 30;
  }

  return std::pair<int, int>(fu_tsumo, fu_ron);
}
