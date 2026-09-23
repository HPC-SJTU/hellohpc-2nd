#include "mahjong_util.hpp"

#include <algorithm>

int factorial(int x) {
  int res = 1;
  for (int i = 1; i <= x; i++) {
    res = res * i;
  }
  return res;
}

int combination(int n, int k) {
  if (k > n) {
    return 0;
  } else {
    return factorial(n) / factorial(k) / factorial(n - k);
  }
}

float my_logit(const float x) {
  if (x >= 1.0) {
    return 10.0;
  } else if (x <= 0.0) {
    return -10.0;
  } else {
    return std::max(-10.0, std::min(10.0, log(x / (1.0 - x))));
  }
}

float logistic(const float *const w, const float *const x, const int DVec) {
  float a = 0.0;
  for (int i = 0; i < DVec; i++) {
    a += w[i] * x[i];
  }
  return 1.0 / (1.0 + exp(-a));
}

void MC_logistic(const float *const w, const float *const x, float *p, const int DVec,
                 const int NClass) {
  float *a = new float[NClass];
  float den = 0.0;
  for (int nc = 0; nc < NClass; nc++) {
    a[nc] = 0.0;
    for (int dv = 0; dv < DVec; dv++) {
      a[nc] = a[nc] + w[DVec * nc + dv] * x[dv];
    }
    p[nc] = exp(a[nc]);
    den = den + p[nc];
  }

  for (int nc = 0; nc < NClass; nc++) {
    p[nc] = p[nc] / den;
  }
  delete[] a;
}

void MC_logistic_mod(const float *const w, const float *const x, float *p, const int DVec,
                     const int NClass) {
  float *a = new float[NClass];
  float den = 0.0;
  float max_elem;
  for (int nc = 0; nc < NClass; nc++) {
    a[nc] = 0.0;
    for (int dv = 0; dv < DVec; dv++) {
      a[nc] = a[nc] + w[DVec * nc + dv] * x[dv];
    }
    max_elem = nc == 0 ? a[nc] : std::max(max_elem, a[nc]);
  }

  for (int nc = 0; nc < NClass; nc++) {
    p[nc] = exp(a[nc] - max_elem);
    den = den + p[nc];
  }

  for (int nc = 0; nc < NClass; nc++) {
    p[nc] = p[nc] / den;
  }
  delete[] a;
}

bool is_red_tile(const int tile) { return tile > 0 && tile % 10 == 0; }

int mod_pid(const int round, const int dealer, const int pid) {
  const int origin_dealer = (12 + dealer - round) % 4;
  return (4 + pid - origin_dealer) % 4;
}

int get_other_riichi_declared_num(const int my_pid, const Game_State &game_state) {
  int ret = 0;
  for (int pid = 0; pid < 4; pid++) {
    if (pid != my_pid && game_state.player_state[pid].riichi_declared) {
      ret++;
    }
  }
  return ret;
}

int get_max_other_open_meld_num(const int my_pid, const Game_State &game_state) {
  int ret = 0;
  for (int pid = 0; pid < 4; pid++) {
    if (pid != my_pid) {
      ret = std::max(ret, (int)game_state.player_state[pid].open_meld.size());
    }
  }
  return ret;
}

Tile_Array get_tile_visible_all(const Game_State &game_state) {
  Tile_Array tile_visible_all;
  for (int tile = 0; tile < 38; tile++) {
    tile_visible_all[tile] = 0;
  }
  for (int pid = 0; pid < 4; pid++) {
    const Player_State &player_state = game_state.player_state[pid];
    for (int kn = 0; kn < player_state.river.size(); kn++) {
      tile_visible_all[player_state.river[kn].tile]++;
    }
    for (int fn = 0; fn < player_state.open_meld.size(); fn++) {
      for (int i = 0; i < player_state.open_meld[fn].consumed.size(); i++) {
        tile_visible_all[player_state.open_meld[fn].consumed[i]]++;
      }
    }
  }
  for (int dn = 0; dn < game_state.dora_marker.size(); dn++) {
    tile_visible_all[game_state.dora_marker[dn]]++;
  }
  for (int c = 0; c < 3; c++) {
    if (tile_visible_all[c * 10 + 10] > 1) {
      tile_visible_all[c * 10 + 5] += tile_visible_all[c * 10 + 10] - 1;
      tile_visible_all[c * 10 + 10] = 1;
    }
    // Prevent counting a red five twice when it is exposed and then upgraded to a kan.
  }
  return tile_visible_all;
}

int get_remaining_tile_num(const int my_pid, const Game_State &game_state) {
  Tile_Array tile_visible_all = get_tile_visible_all(game_state);
  int res = 136 - std::accumulate(tile_visible_all.begin(), tile_visible_all.end(), 0);
  return res - std::accumulate(game_state.player_state[my_pid].hand.begin(),
                               game_state.player_state[my_pid].hand.end(), 0);
}

Tile_Array get_tile_visible_wo_hand(const int my_pid, const Game_State &game_state) {
  Tile_Array tile_visible = get_tile_visible_all(game_state);
  for (int fn = 0; fn < game_state.player_state[my_pid].open_meld.size(); fn++) {
    for (int i = 0; i < game_state.player_state[my_pid].open_meld[fn].consumed.size(); i++) {
      tile_visible[game_state.player_state[my_pid].open_meld[fn].consumed[i]]--;
    }
    if (game_state.player_state[my_pid].open_meld[fn].type != FT_CONCEALED_KAN) {
      tile_visible[game_state.player_state[my_pid].open_meld[fn].tile]--;
    }
  }
  return tile_visible;
}

Tile_Array get_tile_visible_me(const int my_pid, const Game_State &game_state) {
  Tile_Array tile_visible = get_tile_visible_all(game_state);
  for (int tile = 1; tile < 38; tile++) {
    tile_visible[tile] += game_state.player_state[my_pid].hand[tile];
  }
  return tile_visible;
}

Tile_Array sum_tile_array(const Tile_Array &tile_array1, const Tile_Array &tile_array2) {
  Tile_Array tile_array_sum;
  for (int tile = 0; tile < 38; tile++) {
    tile_array_sum[tile] = tile_array1[tile] + tile_array2[tile];
  }
  return tile_array_sum;
}

Tile_Array cal_remaining_kind_array(const Tile_Array &visible_kind) {
  Tile_Array remaining_kind_array;
  for (int tile = 0; tile < 38; tile++) {
    if (tile % 10 == 0) {
      remaining_kind_array[tile] = 0;
    } else {
      remaining_kind_array[tile] = 4 - visible_kind[tile];
    }
  }
  return remaining_kind_array;
}

std::array<bool, 38> get_discard_kind(const River &river) {
  std::array<bool, 38> discard_kind;
  std::fill(discard_kind.begin(), discard_kind.end(), false);
  for (int i = 0; i < river.size(); i++) {
    discard_kind[tile_kind(river[i].tile)] = true;
  }
  return discard_kind;
}

std::array<bool, 38> get_decline_win(const Moves &game_record, const int target) {
  // If no hand discard follows, consider the tile as declined. This differs from furiten under the
  // rules.
  std::array<bool, 38> decline_win;
  std::fill(decline_win.begin(), decline_win.end(), false);
  for (int i = game_record.size() - 1; 0 <= i; i--) {
    if (game_record[i].type == EventType::ROUND) {
      break;
    } else if (game_record[i].type == EventType::DISCARD) {
      if (game_record[i].player == target && !game_record[i].from_draw) {
        break;
      } else {
        decline_win[tile_kind(game_record[i].tile)] = true;
      }
    }
  }
  return decline_win;
}

std::array<bool, 38> get_decline_win_ar(const Moves &game_record, const Game_State &game_state,
                                        const int target) {
  std::array<bool, 38> decline_win_ar;
  std::fill(decline_win_ar.begin(), decline_win_ar.end(), false);
  if (game_state.player_state[target].riichi_declared) {
    for (int i = game_record.size() - 1; 0 <= i; i--) {
      if (game_record[i].type == EventType::RIICHI && game_record[i].player == target) {
        break;
      } else if (game_record[i].type == EventType::DISCARD) {
        decline_win_ar[tile_kind(game_record[i].tile)] = true;
      }
    }
  }
  return decline_win_ar;
}

std::array<bool, 38> get_discard_before_riichi(const River &river) {
  std::array<bool, 38> discard_before_riichi;
  std::fill(discard_before_riichi.begin(), discard_before_riichi.end(), false);

  for (int i = 0; i < river.size(); i++) {
    discard_before_riichi[river[i].tile] = true;
    // TODO: Determine whether a red-five discard also marks the non-red five.
    if (river[i].is_riichi) {
      break;
    }
  }
  return discard_before_riichi;
}

int count_hand_discard_num(const River &river) {
  int hand_discard_num = 0;
  for (int i = 0; i < river.size(); i++) {
    if (!river[i].discard_from_draw) {
      hand_discard_num++;
    }
  }
  return hand_discard_num;
}

std::array<std::array<bool, 38>, 38> get_proto_sequence(const Game_State &game_state,
                                                        const int target) {
  std::array<std::array<bool, 38>, 38> proto_sequence;
  std::array<bool, 38> discard;
  // If tile1 was discarded as the last hand discard or earlier, proto_sequence[tile1][0] = 1.
  // If tile2 was discarded by hand after tile1, proto_sequence[tile1][tile2] = 1.
  for (int tile1 = 0; tile1 < 38; tile1++) {
    discard[tile1] = false;
    for (int tile2 = 0; tile2 < 38; tile2++) {
      proto_sequence[tile1][tile2] = false;
    }
  }

  for (const auto &discarded_tile : game_state.player_state[target].river) {
    discard[tile_kind(discarded_tile.tile)] = true;
    if (!discarded_tile.discard_from_draw) {
      for (int tile = 0; tile < 38; tile++) {
        if (proto_sequence[tile][0]) {
          proto_sequence[tile][tile_kind(discarded_tile.tile)] = true;
        } else if (discard[tile]) {
          proto_sequence[tile][0] = true;
          if (tile != tile_kind(discarded_tile.tile)) {
            proto_sequence[tile][tile_kind(discarded_tile.tile)] = true;
          }
        }
      }
    }
  }
  return proto_sequence;
}

std::array<std::array<std::array<bool, 38>, 7>, 3> get_proto_sequence_open_wait(
    const Game_State &game_state, const int target) {
  const std::array<std::array<bool, 38>, 38> proto_sequence =
      get_proto_sequence(game_state, target);
  std::array<std::array<std::array<bool, 38>, 7>, 3> proto_sequence_open_wait;
  for (int j = 0; j < 3; j++) {
    for (int i = 0; i < 7; i++) {
      for (int tile = 0; tile < 38; tile++) {
        proto_sequence_open_wait[j][i][tile] = false;
      }
    }
  }
  // If a tile of the open wait proto sequence at j,i was discarded and then tile was discarded by
  // hand, proto_sequence_open_wait[j][i][tile] = true.
  for (int j = 0; j < 3; j++) {
    for (int i = 1; i <= 6; i++) {
      if (proto_sequence[10 * j + i + 1][0]) {
        proto_sequence_open_wait[j][i][0] = true;
        for (int tile = 1; tile < 38; tile++) {
          if (proto_sequence[10 * j + i + 1][tile]) {
            proto_sequence_open_wait[j][i][tile] = true;
          }
        }
      }
      if (proto_sequence[10 * j + i + 2][0]) {
        proto_sequence_open_wait[j][i][0] = true;
        for (int tile = 1; tile < 38; tile++) {
          if (proto_sequence[10 * j + i + 2][tile]) {
            proto_sequence_open_wait[j][i][tile] = true;
          }
        }
      }
    }
  }
  return proto_sequence_open_wait;
}

std::array<std::array<std::array<bool, 38>, 9>, 3> get_proto_sequence_kanchan(
    const Game_State &game_state, const int target) {
  const std::array<std::array<bool, 38>, 38> proto_sequence =
      get_proto_sequence(game_state, target);
  std::array<std::array<std::array<bool, 38>, 9>, 3> proto_sequence_kanchan;
  for (int j = 0; j < 3; j++) {
    for (int i = 0; i < 9; i++) {
      for (int tile = 0; tile < 38; tile++) {
        proto_sequence_kanchan[j][i][tile] = false;
      }
    }
  }
  for (int j = 0; j < 3; j++) {
    for (int i = 2; i <= 8; i++) {
      if (proto_sequence[10 * j + i - 1][0]) {
        proto_sequence_kanchan[j][i][0] = true;
        for (int tile = 1; tile < 38; tile++) {
          if (proto_sequence[10 * j + i - 1][tile]) {
            proto_sequence_kanchan[j][i][tile] = true;
          }
        }
      }
      if (proto_sequence[10 * j + i + 1][0]) {
        proto_sequence_kanchan[j][i][0] = true;
        for (int tile = 1; tile < 38; tile++) {
          if (proto_sequence[10 * j + i + 1][tile]) {
            proto_sequence_kanchan[j][i][tile] = true;
          }
        }
      }
    }
  }
  return proto_sequence_kanchan;
}

std::array<std::array<std::array<bool, 38>, 2>, 3> get_proto_sequence_edge_wait(
    const Game_State &game_state, const int target) {
  const std::array<std::array<bool, 38>, 38> proto_sequence =
      get_proto_sequence(game_state, target);
  std::array<std::array<std::array<bool, 38>, 2>, 3> proto_sequence_edge_wait;
  for (int j = 0; j < 3; j++) {
    for (int i = 0; i < 2; i++) {
      for (int tile = 0; tile < 38; tile++) {
        proto_sequence_edge_wait[j][i][tile] = false;
      }
    }
  }
  for (int j = 0; j < 3; j++) {
    if (proto_sequence[10 * j + 1][0]) {
      proto_sequence_edge_wait[j][0][0] = true;
      for (int tile = 1; tile < 38; tile++) {
        if (proto_sequence[10 * j + 1][tile]) {
          proto_sequence_edge_wait[j][0][tile] = true;
        }
      }
    }
    if (proto_sequence[10 * j + 2][0]) {
      proto_sequence_edge_wait[j][0][0] = true;
      for (int tile = 1; tile < 38; tile++) {
        if (proto_sequence[10 * j + 2][tile]) {
          proto_sequence_edge_wait[j][0][tile] = true;
        }
      }
    }

    if (proto_sequence[10 * j + 8][0]) {
      proto_sequence_edge_wait[j][1][0] = true;
      for (int tile = 1; tile < 38; tile++) {
        if (proto_sequence[10 * j + 8][tile]) {
          proto_sequence_edge_wait[j][1][tile] = true;
        }
      }
    }
    if (proto_sequence[10 * j + 9][0]) {
      proto_sequence_edge_wait[j][1][0] = true;
      for (int tile = 1; tile < 38; tile++) {
        if (proto_sequence[10 * j + 9][tile]) {
          proto_sequence_edge_wait[j][1][tile] = true;
        }
      }
    }
  }
  return proto_sequence_edge_wait;
}

int tile_dora_han(const std::vector<int> &dora_markerv, int tile) {
  int res = 0;
  for (const int dm : dora_markerv) {
    if (tile_kind(tile) == dora_marker_to_dora(dm)) {
      res++;
    }
  }
  if (tile > 0 && tile % 10 == 0) {
    res++;
  }
  return res;
}

void han_prob_shift(std::array<float, 14> &han_prob, const int shift_num) {
  std::array<float, 14> han_prob_tmp = {};
  for (int han = 0; han <= 13; han++) {
    han_prob_tmp[std::min(han + shift_num, 13)] += han_prob[han];
  }
  for (int han = 0; han < 14; han++) {
    han_prob[han] = han_prob_tmp[han];
  }
}

void hanfu_prob_han_shift(std::array<std::array<float, 12>, 14> &hanfu_prob, const int shift_num) {
  std::array<std::array<float, 12>, 14> hanfu_prob_tmp;
  for (int han = 0; han < 14; han++) {
    for (int fu = 0; fu < 12; fu++) {
      hanfu_prob_tmp[han][fu] = 0.0;
    }
  }

  for (int han = 0; han < 14; han++) {
    for (int fu = 0; fu < 12; fu++) {
      hanfu_prob_tmp[std::min(han + shift_num, 13)][fu] += hanfu_prob[han][fu];
    }
  }
  for (int han = 0; han < 14; han++) {
    for (int fu = 0; fu < 12; fu++) {
      hanfu_prob[han][fu] = hanfu_prob_tmp[han][fu];
    }
  }
}

void hanfu_prob_han_shift_with_prob(std::array<std::array<float, 12>, 14> &hanfu_prob,
                                    const std::array<float, 14> &shift_prob) {
  std::array<std::array<std::array<float, 12>, 14>, 14> prob_tmp;
  for (int shanten_num = 0; shanten_num < 14; shanten_num++) {
    for (int han = 0; han < 14; han++) {
      for (int fu = 0; fu < 12; fu++) {
        prob_tmp[shanten_num][han][fu] = hanfu_prob[han][fu];
      }
    }
    hanfu_prob_han_shift(prob_tmp[shanten_num], shanten_num);
  }
  for (int han = 0; han < 14; han++) {
    for (int fu = 0; fu < 12; fu++) {
      hanfu_prob[han][fu] = 0.0;
    }
  }
  for (int shanten_num = 0; shanten_num < 14; shanten_num++) {
    for (int han = 0; han < 14; han++) {
      for (int fu = 0; fu < 12; fu++) {
        hanfu_prob[han][fu] += shift_prob[shanten_num] * prob_tmp[shanten_num][han][fu];
      }
    }
  }
}

std::array<std::array<std::array<float, 12>, 14>, 4> cal_hanfu_prob_kan(
    const std::array<std::array<std::array<float, 12>, 14>, 4> &hanfu_prob,
    const std::array<float, 14> &shift_prob) {
  std::array<std::array<std::array<float, 12>, 14>, 4> hanfu_prob_kan = hanfu_prob;
  for (int pid = 0; pid < 4; pid++) {
    hanfu_prob_han_shift_with_prob(hanfu_prob_kan[pid], shift_prob);
  }
  return hanfu_prob_kan;
}
