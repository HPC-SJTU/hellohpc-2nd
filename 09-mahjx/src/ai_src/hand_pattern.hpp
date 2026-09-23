#pragma once

#include "../share/calc_shanten.hpp"
#include "../share/include.hpp"
#include "combination.hpp"
#include "hand_state2.hpp"
#include "tenpai_estimator.hpp"
#include "yaku_pattern.hpp"

void push_back_all(std::vector<int> &v1, const std::vector<int> &v2);

class Hand_Pattern_Seven_Pairs {
 public:
  Hand_Pattern_Seven_Pairs();
  Hand_Pattern_Seven_Pairs(std::vector<int> h, std::vector<int> k1, std::vector<int> k2);
  void reset_all();

  int head[7][2];
  std::vector<int> remain;
  int shanten_num;
  int head_num;
  int k1_num;
  std::vector<std::vector<int> > tile_in_pattern;

  void cal_shanten();
  void cal_tile_in_pattern();

  void print_info();
};

class Hand_Pattern {
 public:
  Hand_Pattern(int pid);
  Hand_Pattern(int pid, const std::vector<int> &h, const std::vector<int> &m,
               const std::vector<int> &t, const std::vector<int> &k1, const std::vector<int> &k2,
               const Hand_State2 &hand_state, int open_meld_num);
  void reset_all();

  int my_pid;
  int meld[4][3];
  int head[2];
  std::vector<int> remain;
  int shanten_num;
  std::vector<std::vector<int> > tile_in_block[5];
  std::vector<std::vector<int> > tile_in_pattern;

  void cal_shanten();
  void cal_tile_in_block();
  int is_in_remain(std::vector<int> test);
  int check_tile_in_error(const Tile_Array &hand_kind, const Open_Meld_Vector &open_meld_kind,
                          const int i0, const int i1, const int i2, const int i3, const int i4);
  void cal_tile_in_pattern(const Tile_Array &hand_kind, const Open_Meld_Vector &open_meld_kind);
  int yaku_check(int nip);
  double cal_priority(const Game_State &game_state, const Tile_Array &tile_visible_kind, int nip);

  void print_info();
};
