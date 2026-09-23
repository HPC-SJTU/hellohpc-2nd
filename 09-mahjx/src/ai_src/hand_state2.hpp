#pragma once

#include "../share/include.hpp"
#include "../share/types.hpp"

class Hand_State2 {
 private:
  uint32_t chii[3];
  uint32_t pon_kan[4];
  uint32_t riichi_red;

 public:
  Hand_State2();
  Hand_State2(const Tile_Array &hand, const bool rf, const Open_Meld_Vector &open_meld);
  void reset();
  void reset_open_meld();
  void reset_with(const Tile_Array &hand, const bool rf, const Open_Meld_Vector &open_meld);

  Hand_State2 &operator=(const Hand_State2 &rhs);
  bool operator==(const Hand_State2 &rhs) const;
  bool operator!=(const Hand_State2 &rhs) const;

  void set_red_inside(int color, int flag);
  void set_red_outside(int color, int flag);
  void set_riichi(int flag);
  void set_open_meld(const Open_Meld_Vector &open_meld);

  void add_chii(int smallest_tile_kind);
  void delete_chii(int smallest_tile_kind);
  void add_pon(int pon_tile_kind);
  void delete_pon(int pon_tile_kind);
  void add_concealed_kan(int concealed_kan_tile_kind);
  void delete_concealed_kan(int concealed_kan_tile_kind);
  void add_open_kan(int open_kan_tile_kind);
  void delete_open_kan(int open_kan_tile_kind);
  void add_one_open_meld(const Open_Meld_Type open_meld_type, const int smallest_tile_kind);
  void delete_one_open_meld(const Open_Meld_Type open_meld_type, int smallest_tile_kind);

  int get_riichi() const;
  int get_chii_num(const int smallest_tile_kind) const;
  int get_pon_num(const int tile_kind) const;
  int get_concealed_kan_num(const int tile_kind) const;
  int get_open_kan_num(const int tile_kind) const;
  int get_red_inside(const int color) const;
  int get_red_outside(const int color) const;
  int get_tile_kind_num(const int tile_kind) const;

  Open_Meld_Vector get_open_meld(const Hand_State2 &present_hand_state) const;
  void get_open_meld(int open_meld[4][6], const Hand_State2 &present_hand_state) const;

  void print_info();
  size_t hash_value() const;
};

size_t hash_value(const Hand_State2 &hand_state2);

bool is_same_open_meld(const Hand_State2 &ts1, const Hand_State2 &ts2);
bool is_ts_open_meld_consistent(const Hand_State2 &hand_state, const Open_Meld_Vector &open_meld);
