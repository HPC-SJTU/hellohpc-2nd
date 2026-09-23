#include "win.hpp"

int fu_to_index(const int fu) {
  if (fu == 20) {
    return 1;
  } else if (fu == 25) {
    return 2;
  } else {
    return (fu + 9) / 10;
  }
}

int fu_index_to_fu(const int fu_index) {
  if (fu_index == 1) {
    return 20;
  } else if (fu_index == 2) {
    return 25;
  } else {
    return fu_index * 10;
  }
}

Win_Info_Bit::Win_Info_Bit() { reset(); }

void Win_Info_Bit::reset() { data = 0; }

int Win_Info_Bit::get_han_tsumo() const { return data & 0x0000000F; }

int Win_Info_Bit::get_han_ron() const { return (data & 0x000000F0) >> 4; }

int Win_Info_Bit::get_fu_index_tsumo() const { return (data & 0x00000F00) >> 8; }

int Win_Info_Bit::get_fu_index_ron() const { return (data & 0x0000F000) >> 12; }

int Win_Info_Bit::get_tile() const { return (data & 0x00FF0000) >> 16; }

int Win_Info_Bit::get_dora_num() const { return (data & 0x0F000000) >> 24; }

void Win_Info_Bit::set_han_tsumo(const int han) {
  data = (data & 0xFFFFFFF0) + uint32_t(std::min(han, 13));
}

void Win_Info_Bit::set_han_ron(const int han) {
  data = (data & 0xFFFFFF0F) + (uint32_t(std::min(han, 13)) << 4);
}

void Win_Info_Bit::set_fu_index_tsumo(const int fu_index) {
  assert(0 <= fu_index && fu_index < 16);
  data = (data & 0xFFFFF0FF) + (uint32_t(fu_index) << 8);
}

void Win_Info_Bit::set_fu_index_ron(const int fu_index) {
  assert(0 <= fu_index && fu_index < 16);
  data = (data & 0xFFFF0FFF) + (uint32_t(fu_index) << 12);
}

void Win_Info_Bit::set_tile(const int tile) {
  assert(0 <= tile && tile < 38);
  data = (data & 0xFF00FFFF) + (uint32_t(tile) << 16);
}

void Win_Info_Bit::set_dora_num(const int dora_num) {
  data = (data & 0xF0FFFFFF) + (uint32_t(std::min(dora_num, 13)) << 24);
}

Win_Basic::Win_Basic() {}

Win_Calc::Win_Calc() : Win_Basic() {}

std::array<double, 5> calc_ura_elem(const Tile_Array &visible_me_kind, const Tile_Array &hand) {
  std::array<double, 5> ura_elem = {};
  Tile_Array hand_kind = tile_kind(hand);

  int invisible_total = 0;
  for (int tile = 1; tile < 38; tile++) {
    if (tile % 10 != 0) {
      ura_elem[hand_kind[dora_marker_to_dora(tile)]] += 4 - visible_me_kind[tile];
      invisible_total += 4 - visible_me_kind[tile];
    }
  }
  for (int i = 0; i < 5; i++) {
    ura_elem[i] = ura_elem[i] / invisible_total;
  }
  return ura_elem;
}

std::array<double, 13> multiply_ura_prob(const std::array<double, 5> &ura_elem,
                                         const int dora_num) {
  std::array<double, 13> ura_prob_tmp = {};
  std::array<double, 13> ura_prob = {};

  ura_prob[0] = 1.0;
  for (int dn = 0; dn < dora_num; dn++) {
    for (int i = 0; i < 13; i++) {
      ura_prob_tmp[i] = ura_prob[i];
      ura_prob[i] = 0.0;
    }
    for (int i = 0; i < 13; i++) {
      for (int j = 0; j < 5; j++) {
        if (i + j >= 12) {
          ura_prob[12] += ura_prob_tmp[i] * ura_elem[j];
        } else {
          ura_prob[i + j] += ura_prob_tmp[i] * ura_elem[j];
        }
      }
    }
  }
  return ura_prob;
}

std::array<double, 13> Win_Basic::calc_ura_prob(const Bit_Tile_Num &hand_bit,
                                                const Hand_State2 &hand_state,
                                                const Tile_Array &tile_visible_kind,
                                                const int dora_num) const {
  if (hand_state.get_riichi() == 0) {
    std::array<double, 13> ura_prob{};
    ura_prob[0] = 1.0;
    return ura_prob;
  } else {
    Tile_Array hand_kind_counts_wm = {};
    for (int tile = 1; tile < 38; tile++) {
      if (tile % 10 != 0) {
        hand_kind_counts_wm[tile] += hand_bit.count_tile(tile);
        hand_kind_counts_wm[tile] += hand_state.get_tile_kind_num(tile);
      }
    }
    assert(hand_kind_counts_wm[win_info.get_tile()] < 4);
    hand_kind_counts_wm[win_info.get_tile()]++;
    std::array<double, 5> ura_elem = calc_ura_elem(tile_visible_kind, hand_kind_counts_wm);
    return multiply_ura_prob(ura_elem, dora_num);
  }
}

std::array<double, 2> Win_Basic::get_points_exp_direct(
    const int my_pid, const int target, const int incident_han, const Bit_Tile_Num &hand_bit,
    const Hand_State2 &hand_state, const Tile_Array &tile_visible_kind,
    const Game_State &game_state,
    const std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> &round_end_pt_exp)
    const {
  std::array<double, 2> result;
  for (int i = 0; i < 2; i++) {
    result[i] = -200.0;
  }
  if (get_points_tsumo(my_pid, game_state) > 0 || incident_han > 0) {
    const int win_han =
        (my_pid == target) ? win_info.get_han_tsumo() : (win_info.get_han_ron() + incident_han);
    std::array<double, 13> ura_prob =
        calc_ura_prob(hand_bit, hand_state, tile_visible_kind, game_state.dora_marker.size());
    if (win_han > 0) {
      const int fu_index =
          my_pid == target ? win_info.get_fu_index_tsumo() : win_info.get_fu_index_ron();

      double tmp = 0.0;
      for (int un = 0; un < 13; un++) {
        int han_tmp = std::min(13, un + win_han);
        tmp += round_end_pt_exp[my_pid][target][han_tmp][fu_index] * ura_prob[un];
      }
      result[0] = tmp;
      if (win_info.get_tile() % 10 == 5 && win_info.get_tile() < 30) {
        tmp = 0.0;
        for (int un = 0; un < 13; un++) {
          int han_tmp = std::min(13, un + win_han + 1);
          tmp += round_end_pt_exp[my_pid][target][han_tmp][fu_index] * ura_prob[un];
        }
        result[1] = tmp;
      }
    }
  }
  return result;
}

std::array<double, 4> Win_Basic::get_points_exp(
    const int my_pid, const Bit_Tile_Num &hand_bit, const Hand_State2 &hand_state,
    const Tile_Array &tile_visible_kind, const Game_State &game_state,
    const std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> &round_end_pt_exp,
    const int last_draw_han) const {
  std::array<double, 4> result;
  for (int i = 0; i < 4; i++) {
    result[i] = -200.0;
  }

  if (get_points_tsumo(my_pid, game_state) > 0 || last_draw_han > 0) {
    int win_han = win_info.get_han_tsumo() + last_draw_han;
    std::array<double, 13> ura_prob =
        calc_ura_prob(hand_bit, hand_state, tile_visible_kind, game_state.dora_marker.size());
    if (win_han > 0) {
      const int fu_index = win_info.get_fu_index_tsumo();
      double tmp = 0.0;
      for (int un = 0; un < 13; un++) {
        int han_tmp = std::min(13, un + win_han);
        tmp += round_end_pt_exp[my_pid][my_pid][han_tmp][fu_index] * ura_prob[un];
      }
      result[0] = tmp;

      if (win_info.get_tile() % 10 == 5 && win_info.get_tile() < 30) {
        tmp = 0.0;
        for (int un = 0; un < 13; un++) {
          int han_tmp = std::min(13, un + win_han + 1);
          tmp += round_end_pt_exp[my_pid][my_pid][han_tmp][fu_index] * ura_prob[un];
        }
        result[2] = tmp;
      }
    }
    win_han = win_info.get_han_ron();
    if (win_han > 0) {
      const int fu_index = win_info.get_fu_index_ron();
      double tmp = 0.0;
      for (int pid = 0; pid < 4; pid++) {
        if (pid != my_pid) {
          for (int un = 0; un < 13; un++) {
            int han_tmp = std::min(13, un + win_han);
            tmp += round_end_pt_exp[my_pid][pid][han_tmp][fu_index] * ura_prob[un];
          }
        }
      }
      tmp = tmp / 3.0;
      result[1] = tmp;
      if (win_info.get_tile() % 10 == 5 && win_info.get_tile() < 30) {
        tmp = 0.0;
        for (int pid = 0; pid < 4; pid++) {
          if (pid != my_pid) {
            for (int un = 0; un < 13; un++) {
              int han_tmp = std::min(13, un + win_han + 1);
              tmp += round_end_pt_exp[my_pid][pid][han_tmp][fu_index] * ura_prob[un];
            }
          }
        }
        tmp = tmp / 3.0;
        result[3] = tmp;
      }
    }
  }
  return result;
}

int Win_Basic::get_points_tsumo(const int pid, const Game_State &game_state) const {
  if (win_info.get_han_tsumo() == 0) {
    return 0;
  } else {
    return tsumo_win(win_info.get_han_tsumo(), fu_index_to_fu(win_info.get_fu_index_tsumo()),
                     game_state.player_state[pid].self_wind == 0);
  }
}

int Win_Basic::get_points_ron(const int pid, const Game_State &game_state) const {
  if (win_info.get_han_ron() == 0) {
    return 0;
  } else {
    return ron_win(win_info.get_han_ron(), fu_index_to_fu(win_info.get_fu_index_ron()),
                   game_state.player_state[pid].self_wind == 0);
  }
}

Win_Basic win_info_to_win_basic(const Win_Info &win_info) {
  Win_Basic win_basic;
  win_basic.win_info.set_han_tsumo(win_info.han_tsumo);
  win_basic.win_info.set_han_ron(win_info.han_ron);
  win_basic.win_info.set_fu_index_tsumo(fu_to_index(win_info.fu_tsumo));
  win_basic.win_info.set_fu_index_ron(fu_to_index(win_info.fu_ron));
  win_basic.win_info.set_tile(win_info.tile);
  return win_basic;
}

Win_Calc win_info_to_win_calc(const Win_Info &win_info) {
  Win_Calc win_calc;
  win_calc.win_info.data = win_info_to_win_basic(win_info).win_info.data;
  win_calc.tsumo_exp = -200.0;
  win_calc.ron_exp = -200.0;
  return win_calc;
}
