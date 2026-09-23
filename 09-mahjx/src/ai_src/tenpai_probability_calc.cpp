#include "tenpai_probability_calc.hpp"

#include "params.hpp"

float infer_tenpai_prob_old(const River &river, const int open_meld_num) {
  // The reference implementation uses this in cal_remaining_exp.
  const int hand_discard_num = count_hand_discard_num(river);

  const std::array<bool, 38> discarded_kind = get_discard_kind(river);
  int discard[38];
  for (int tile = 0; tile < 38; tile++) {
    discard[tile] = discarded_kind[tile] ? 1 : 0;
  }

  int discard_kind[6] = {};
  discard_kind[0] = discard[31] + discard[32] + discard[33] + discard[34] + discard[35] +
                    discard[36] + discard[37];
  discard_kind[1] = discard[1] + discard[9] + discard[11] + discard[19] + discard[21] + discard[29];
  discard_kind[2] = discard[2] + discard[8] + discard[12] + discard[18] + discard[22] + discard[28];
  discard_kind[3] = discard[3] + discard[7] + discard[13] + discard[17] + discard[23] + discard[27];
  discard_kind[4] = discard[4] + discard[6] + discard[14] + discard[16] + discard[24] + discard[26];
  for (int i = 0; i < 3; i++) {
    if (discard[i * 10 + 5] || discard[i * 10 + 10]) {
      discard_kind[5]++;
    }
  }

  const int DVec = 9;
  int x[DVec];
  x[0] = 1.0;
  x[1] = open_meld_num;
  x[2] = hand_discard_num;
  for (int i = 0; i < 6; i++) {
    x[3 + i] = discard_kind[i];
  }

  double w[DVec];
  w[0] = -4.9433334901103434e+000;
  w[1] = 1.2079080379420084e+000;
  w[2] = 4.9703758302681988e-003;
  w[3] = 2.2804536169498757e-001;
  w[4] = 1.3372123812601580e-001;
  w[5] = 3.4313212938013748e-001;
  w[6] = 4.0650499365173909e-001;
  w[7] = 4.8931336938797670e-001;
  w[8] = 3.6756628774530170e-001;

  double a = 0.0;
  for (int i = 0; i < DVec; i++) {
    a = a + w[i] * x[i];
  }

  return 1.0 / (1.0 + exp(-a));
}

float infer_tenpai_prob(const Moves &game_record, const Game_State &game_state, const int target) {
  const int fn = game_state.player_state[target].open_meld.size();
  if (fn == 4) {
    return 1.0;
  }
  const int kn = game_state.player_state[target].river.size();
  const int lb = [&] {
    if (fn == 0) {
      return 2;
    } else if (fn == 1) {
      return 2;
    } else if (fn == 2) {
      return 3;
    } else if (fn == 3) {
      return 7;
    } else {
      assert_with_out(false, "infer_tenpai_prob lb_error");
      return 0;
    }
  }();
  const int ub = 18;
  const float *const params = OPEN_MELD_PARA[fn][std::min(std::max(lb, kn), ub)];
  float w[5];
  for (int i = 0; i < 5; i++) w[i] = params[i];

  float x[5];

  bool other_riichi = false;
  int discard_after_other_riichi = 0;
  int round_begin = -1;
  for (int i = game_record.size() - 1; 0 <= i; i--) {
    if (game_record[i].type == EventType::ROUND) {
      round_begin = i;
      break;
    }
  }
  assert(round_begin != -1);

  for (int i = round_begin; i < game_record.size(); i++) {
    const Event &action = game_record[i];
    if (action.type == EventType::GAME || action.type == EventType::END_GAME ||
        action.type == EventType::WIN) {
      assert(false);
    } else if (action.type == EventType::ROUND) {
      continue;
    } else if (action.type == EventType::RIICHI) {
      if (action.player != target) {
        other_riichi = true;
      }
    } else if (action.type == EventType::DISCARD) {
      if (action.player == target && other_riichi) {
        discard_after_other_riichi++;
      }
    } else if (action.type == EventType::CHII || action.type == EventType::PON) {
      if (action.player == target && other_riichi) {
        discard_after_other_riichi = -1;
        // Currently this process for discard_after_other_riichi is done only in the reference
        // implementation.
      }
    }
  }

  int discard_kind[6] = {};
  const std::array<bool, 38> discarded_kind =
      get_discard_kind(game_state.player_state[target].river);
  for (int tile = 0; tile < 38; tile++) {
    if (discarded_kind[tile]) {
      if (30 < tile) {
        discard_kind[0]++;
      } else if (tile % 10 == 1 || tile % 10 == 9) {
        discard_kind[1]++;
      } else if (tile % 10 == 2 || tile % 10 == 8) {
        discard_kind[2]++;
      } else if (tile % 10 == 3 || tile % 10 == 7) {
        discard_kind[3]++;
      } else if (tile % 10 == 4 || tile % 10 == 6) {
        discard_kind[4]++;
      } else if (tile % 10 == 5) {
        discard_kind[5]++;
      } else {
        assert(false);
      }
    }
  }

  x[0] = 1.0;
  x[1] = (float)count_hand_discard_num(game_state.player_state[target].river);
  x[2] = (float)discard_after_other_riichi;
  x[3] = (float)(discard_kind[0] + discard_kind[1]);
  x[4] = (float)(discard_kind[2] + discard_kind[3] + discard_kind[4] + discard_kind[5]);

  if (fn == 0) {
    return std::min((float)0.12, logistic(w, x, 5));
  } else {
    return logistic(w, x, 5);
  }
}

bool flushing_possible(const Open_Meld_Vector &open_meld, const Color_Type color) {
  for (const Open_Meld_Elem &f : open_meld) {
    if (tile_color(f.consumed[0]) != color && tile_color(f.consumed[0]) != CT_HONOR_TILE) {
      return false;
    }
  }
  return true;
}

std::array<int, 4> get_flushing_feature(const River &river, const Color_Type color) {
  int nclength = 0;
  int ncstart = 0;
  int nclength_tmp = 0;
  int ncstart_tmp = 0;
  for (int kn = 0; kn < river.size(); kn++) {
    if (tile_color(river[kn].tile) == color || tile_color(river[kn].tile) == CT_HONOR_TILE) {
      if (nclength_tmp > nclength) {
        nclength = nclength_tmp;
        ncstart = ncstart_tmp;
      }
      nclength_tmp = 0;
      ncstart_tmp = kn + 1;
    } else {
      nclength_tmp++;
    }
  }

  if (nclength_tmp > nclength) {
    nclength = nclength_tmp;
    ncstart = ncstart_tmp;
  }

  int before_honor_tile = 0;
  int before_number_tile = 0;

  for (int kn = 0; kn < ncstart; kn++) {
    if (tile_color(river[kn].tile) == CT_HONOR_TILE) {
      before_honor_tile = 1;
    } else if (tile_color(river[kn].tile) == color) {
      before_number_tile = 1;
    }
  }
  int last_hand_discard_color = 0;
  for (int kn = 0; kn < river.size(); kn++) {
    if (!river[kn].discard_from_draw) {
      if (tile_color(river[kn].tile) == color || tile_color(river[kn].tile) == CT_HONOR_TILE) {
        last_hand_discard_color = 1;
      } else {
        last_hand_discard_color = 0;
      }
    }
  }
  std::array<int, 4> feature;
  feature[0] = nclength;
  feature[1] = nclength * last_hand_discard_color;
  feature[2] = before_honor_tile;
  feature[3] = before_number_tile;
  return feature;
}

std::array<int, 2> get_flushing_tenpai_post_feature(const River &river, const Color_Type color) {
  int discard[38] = {};
  for (int i = 0; i < river.size(); i++) {
    discard[river[i].tile] = 1;
  }
  int discard_honor_tile = 0;
  int discard_number_tile = 0;
  for (int i = 0; i < 7; i++) {
    discard_honor_tile += discard[31 + i];
  }
  for (int i = 1; i <= 10; i++) {
    discard_number_tile += discard[10 * color + i];
  }
  std::array<int, 2> feature;
  feature[0] = discard_honor_tile;
  feature[1] = discard_number_tile;
  return feature;
}

float cal_flushing_prob(const Game_State &game_state, const int target, const Color_Type color) {
  if (game_state.player_state[target].riichi_declared ||
      game_state.player_state[target].open_meld.size() == 0) {
    return 0.0;
  } else if (!flushing_possible(game_state.player_state[target].open_meld, color)) {
    return 0.0;
  } else {
    const float *params = nullptr;
    if (game_state.player_state[target].open_meld.size() >= 3) {
      params = FLUSHING_OPEN_MELD[3][std::max(
          7, std::min((int)game_state.player_state[target].river.size(), 18))];
    } else if (game_state.player_state[target].open_meld.size() == 2) {
      params = FLUSHING_OPEN_MELD[2][std::max(
          7, std::min((int)game_state.player_state[target].river.size(), 16))];
    } else if (game_state.player_state[target].open_meld.size() == 1) {
      params = FLUSHING_OPEN_MELD[1][std::max(
          7, std::min((int)game_state.player_state[target].river.size(), 17))];
    } else {
      assert_with_out(false, "cal_flushing_prob error");
    }
    float w[5], x[5];
    for (int i = 0; i < 5; i++) w[i] = params[i];
    std::array<int, 4> feature = get_flushing_feature(game_state.player_state[target].river, color);
    x[0] = 1.0;
    for (int i = 0; i < 4; i++) {
      x[i + 1] = feature[i];
    }
    return logistic(w, x, 5);
  }
}

float cal_flushing_tenpai_post(const Game_State &game_state, const int target,
                               const Color_Type color) {
  const float *params = nullptr;
  if (game_state.player_state[target].open_meld.size() >= 3) {
    params = FLUSHING_TENPAI_OPEN_MELD[3][std::max(
        7, std::min((int)game_state.player_state[target].river.size(), 17))];
  } else if (game_state.player_state[target].open_meld.size() == 2) {
    params = FLUSHING_TENPAI_OPEN_MELD[2][std::max(
        7, std::min((int)game_state.player_state[target].river.size(), 18))];
  } else if (game_state.player_state[target].open_meld.size() == 1) {
    params = FLUSHING_TENPAI_OPEN_MELD[1][std::max(
        7, std::min((int)game_state.player_state[target].river.size(), 17))];
  } else {
    return 0.0;
  }

  float w[3], x[3];
  for (int i = 0; i < 3; i++) w[i] = params[i];
  std::array<int, 2> feature =
      get_flushing_tenpai_post_feature(game_state.player_state[target].river, color);
  x[0] = 1.0;
  for (int i = 0; i < 2; i++) {
    x[i + 1] = feature[i];
  }
  return logistic(w, x, 3);
}
