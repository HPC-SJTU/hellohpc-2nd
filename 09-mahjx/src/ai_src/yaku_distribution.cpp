#include "yaku_distribution.hpp"

int value_tile_dist(const Tile_Array &hand, const Open_Meld_Vector &open_meld, const int value_tile,
                    const Game_State &game_state) {
  for (const Open_Meld_Elem f : open_meld) {
    if (f.consumed[0] == value_tile) {
      return 0;
    }
  }
  if (3 <= hand[value_tile]) {
    return 0;
  }
  const Tile_Array tile_visible_all = get_tile_visible_all(game_state);
  if (2 <= tile_visible_all[value_tile]) {
    return 4;  // For now.
  } else {
    return 3 - hand[value_tile];
  }
}

int half_flush_dist(const Tile_Array &hand, const Open_Meld_Vector &open_meld) {
  int dist = 13;
  int flags[3];
  for (int i = 0; i < 3; i++) {
    flags[i] = 1;
  }
  for (Color_Type color = (Color_Type)0; color < CT_HONOR_TILE; ++color) {
    for (const Open_Meld_Elem f : open_meld) {
      if (tile_color(f.consumed[0]) != color && tile_color(f.consumed[0]) != CT_HONOR_TILE) {
        flags[color] = 0;
        break;
      }
    }
    if (flags[color]) {
      int other_num = 0;
      for (int c2 = 0; c2 < 3; c2++) {
        if (c2 != color) {
          for (int i = 1; i <= 9; i++) {
            other_num += hand[10 * c2 + i];
          }
        }
      }
      dist = std::min(dist, other_num);
    }
  }
  return dist;
}

int simples_dist(const Tile_Array &hand, const Open_Meld_Vector &open_meld) {
  for (const Open_Meld_Elem f : open_meld) {
    if (f.type == FT_CHII) {
      if (tile_terminal_or_honor(f.tile) != NT_SIMPLES ||
          tile_terminal_or_honor(f.consumed[0]) != NT_SIMPLES ||
          tile_terminal_or_honor(f.consumed[1]) != NT_SIMPLES) {
        return 13;
      }
    } else if (tile_terminal_or_honor(f.consumed[0]) != NT_SIMPLES) {
      return 13;
    }
  }
  int dist = 0;
  for (int tile = 0; tile < 38; tile++) {
    if (hand[tile] > 0 && tile_terminal_or_honor(tile) != NT_SIMPLES) {
      dist += hand[tile];
    }
  }
  return dist;
}

int outside_hand_dist(const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                      const Game_State &game_state) {
  // Do not count the pair because it is troublesome.
  for (const Open_Meld_Elem f : open_meld) {
    if (f.type == FT_CHII) {
      if (tile_terminal_or_honor(f.tile) == NT_SIMPLES &&
          tile_terminal_or_honor(f.consumed[0]) == NT_SIMPLES &&
          tile_terminal_or_honor(f.consumed[1]) == NT_SIMPLES) {
        return false;
      }
    } else if (tile_terminal_or_honor(f.consumed[0]) == NT_SIMPLES) {
      return false;
    }
  }

  int dist = 0;
  for (int c = 0; c < 3; c++) {
    dist += hand[10 * c + 4] + hand[10 * c + 5] + hand[10 * c + 6];
  }

  const Tile_Array tile_visible_all = get_tile_visible_all(game_state);
  const Tile_Array tile_visible_all_kind = tile_kind(tile_visible_all);
  for (int c = 0; c < 3; c++) {
    dist += std::max(
        0, hand[10 * c + 2] - std::min(hand[10 * c + 1], 4 - tile_visible_all_kind[10 * c + 3]));
    dist += std::max(
        0, hand[10 * c + 3] - std::min(hand[10 * c + 1], 4 - tile_visible_all_kind[10 * c + 2]));

    dist += std::max(
        0, hand[10 * c + 8] - std::min(hand[10 * c + 9], 4 - tile_visible_all_kind[10 * c + 7]));
    dist += std::max(
        0, hand[10 * c + 7] - std::min(hand[10 * c + 9], 4 - tile_visible_all_kind[10 * c + 8]));
  }

  return dist;
}

int all_triplets_dist(const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                      const Game_State &game_state) {
  int dist = 13;
  for (const Open_Meld_Elem f : open_meld) {
    if (f.type == FT_CHII) {
      return dist;
    }
  }
  dist -= open_meld.size() * 3;
  int triplet_cand = open_meld.size();
  const Tile_Array tile_visible_all = get_tile_visible_all(game_state);
  const Tile_Array tile_visible_all_kind = tile_kind(tile_visible_all);

  for (int tile = 0; tile < 38; tile++) {
    if (3 <= hand[tile]) {
      dist -= 3;
      triplet_cand++;
    }
  }
  for (int tile = 0; tile < 38; tile++) {
    if (hand[tile] == 2 && tile_visible_all_kind[tile] < 2 && triplet_cand < 4) {
      dist -= 2;
      triplet_cand++;
    }
  }
  return dist;
}

int three_color_triplet_dist(const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                             const Game_State &game_state) {
  int triplet[38];

  for (int tile = 0; tile < 38; tile++) {
    if (hand[tile] == 3) {
      triplet[tile] = 1;
    } else {
      triplet[tile] = 0;
    }
  }

  for (const Open_Meld_Elem f : open_meld) {
    if (f.type == FT_PON || f.type == FT_CONCEALED_KAN || f.type == FT_OPEN_KAN ||
        f.type == FT_UPGRADED_KAN) {
      triplet[tile_kind(f.consumed[0])]++;
    }
  }

  const Tile_Array tile_visible_all = get_tile_visible_all(game_state);
  const Tile_Array tile_visible_all_kind = tile_kind(tile_visible_all);
  int dist = 9;
  for (int i = 1; i <= 9; i++) {
    int tmp_dist = 9;
    for (int c = 0; c < 3; c++) {
      const int tile = 10 * c + i;
      if (triplet[tile] == 1) {
        tmp_dist -= 3;
      } else if (tile_visible_all_kind[tile] < 2) {
        tmp_dist -= hand[tile];
      } else {
        tmp_dist += 9;
      }
    }
    dist = std::min(dist, tmp_dist);
  }
  return dist;
}

int three_color_dist(const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                     const Game_State &game_state) {
  int sequence[30] = {};
  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].type == FT_CHII) {
      sequence[std::min(
          {open_meld[fn].tile, open_meld[fn].consumed[0], open_meld[fn].consumed[1]})]++;
    }
  }
  int dist = 9;
  const Tile_Array tile_visible_all = get_tile_visible_all(game_state);
  const Tile_Array tile_visible_all_kind = tile_kind(tile_visible_all);
  for (int i = 1; i <= 7; i++) {
    int tmp_dist = 9;
    for (int c = 0; c < 3; c++) {
      const int tile = 10 * c + i;
      if (0 < sequence[tile]) {
        tmp_dist -= 3;
      } else {
        for (int j = 0; j < 3; j++) {
          if (0 < hand[tile + j]) {
            tmp_dist--;
          } else if (tile_visible_all_kind[tile] == 4) {
            tmp_dist += 9;
          }
        }
      }
    }
    dist = std::min(dist, tmp_dist);
  }
  return dist;
}

int full_straight_dist(const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                       const Game_State &game_state) {
  int have[30] = {};
  for (int fn = 0; fn < open_meld.size(); fn++) {
    if (open_meld[fn].type == FT_CHII) {
      const int smallest =
          std::min({open_meld[fn].tile, open_meld[fn].consumed[0], open_meld[fn].consumed[1]});
      if (((smallest % 10) % 3) == 1) {
        have[smallest] = 1;
        have[smallest + 1] = 1;
        have[smallest + 2] = 1;
      }
    }
  }
  int dist = 9;
  const Tile_Array tile_visible_all = get_tile_visible_all(game_state);
  const Tile_Array tile_visible_all_kind = tile_kind(tile_visible_all);
  for (int c = 0; c < 3; c++) {
    int tmp_dist = 9;
    for (int i = 1; i <= 9; i++) {
      const int tile = 10 * c + i;
      if (have[tile] == 1 || 0 < hand[tile]) {
        tmp_dist--;
      } else if (tile_visible_all_kind[tile] == 4) {
        tmp_dist += 9;
      }
    }
    dist = std::min(dist, tmp_dist);
  }
  return dist;
}

// concealed_triplet_num_count, kan_num_count

double calc_yaku_dist(const int my_pid, const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                      const Game_State &game_state) {
  double dist = 13.0;
  const int round_wind_dist =
      value_tile_dist(hand, open_meld, 31 + game_state.round_wind, game_state);
  if (round_wind_dist <= 1) {
    dist = std::min(dist, 0.5 * round_wind_dist);
  }

  const int self_wind_dist =
      value_tile_dist(hand, open_meld, 31 + game_state.player_state[my_pid].self_wind, game_state);
  if (self_wind_dist <= 1) {
    dist = std::min(dist, 0.5 * self_wind_dist);
  }

  for (int tile = 35; tile < 38; tile++) {
    const int tmp_dist = value_tile_dist(hand, open_meld, tile, game_state);
    if (tmp_dist <= 1) {
      dist = std::min(dist, 0.5 * tmp_dist);
    }
  }
  dist = std::min(dist, 0.6 * half_flush_dist(hand, open_meld));
  dist = std::min(dist, 0.6 * simples_dist(hand, open_meld));
  dist = std::min(dist, (double)outside_hand_dist(hand, open_meld, game_state));
  dist = std::min(dist, (double)all_triplets_dist(hand, open_meld, game_state));
  dist = std::min(dist, (double)three_color_triplet_dist(hand, open_meld, game_state));
  dist = std::min(dist, (double)three_color_dist(hand, open_meld, game_state));
  dist = std::min(dist, (double)full_straight_dist(hand, open_meld, game_state));

  return dist;
}
