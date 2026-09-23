#pragma once

#include "../share/include.hpp"
#include "../share/types.hpp"
#include "../share/win_points.hpp"
#include "bit_tile_count.hpp"
#include "hand_state2.hpp"
#include "turn_calc.hpp"

const int MAX_WIN_NUM_PER_THREAD = 200000;

int fu_to_index(const int fu);
int fu_index_to_fu(const int fu_index);

class Win_Info_Bit {
 public:
  Win_Info_Bit();
  uint32_t data;
  void reset();
  int get_han_tsumo() const;
  int get_han_ron() const;
  int get_fu_index_tsumo() const;
  int get_fu_index_ron() const;
  int get_tile() const;
  int get_dora_num() const;
  void set_han_tsumo(const int han);
  void set_han_ron(const int han);
  void set_fu_index_tsumo(const int fu_index);
  void set_fu_index_ron(const int fu_index);
  void set_tile(const int tile);
  void set_dora_num(const int dora_num);
};

std::array<double, 5> calc_ura_elem(const Tile_Array &visible_me_kind, const Tile_Array &hand);
std::array<double, 13> multiply_ura_prob(const std::array<double, 5> &ura_elem, const int dora_num);

class Win_Basic {
 public:
  Win_Basic();
  Win_Info_Bit win_info;
  int get_points_tsumo(const int pid, const Game_State &game_state) const;
  int get_points_ron(const int pid, const Game_State &game_state) const;
  std::array<double, 13> calc_ura_prob(const Bit_Tile_Num &hand_bit, const Hand_State2 &hand_state,
                                       const Tile_Array &tile_visible_kind,
                                       const int dora_num) const;
  std::array<double, 2> get_points_exp_direct(
      const int my_pid, const int target, const int incident_han, const Bit_Tile_Num &hand_bit,
      const Hand_State2 &hand_state, const Tile_Array &tile_visible_kind,
      const Game_State &game_state,
      const std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> &round_end_pt_exp)
      const;
  std::array<double, 4> get_points_exp(
      const int my_pid, const Bit_Tile_Num &hand_bit, const Hand_State2 &hand_state,
      const Tile_Array &tile_visible_kind, const Game_State &game_state,
      const std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> &round_end_pt_exp,
      const int last_draw_han = 0) const;
};

class Win_Calc : public Win_Basic {
 public:
  Win_Calc();
  double tsumo_exp;
  double ron_exp;
};

Win_Basic win_info_to_win_basic(const Win_Info &win_info);
Win_Calc win_info_to_win_calc(const Win_Info &win_info);
