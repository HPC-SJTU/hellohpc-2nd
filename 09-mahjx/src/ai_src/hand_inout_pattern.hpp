#pragma once

#include "../share/include.hpp"

class Hand_Inout_Pattern {
 public:
  Hand_Inout_Pattern();
  std::vector<int> tile_in_pattern;
  std::vector<int> tile_out_pattern;
  double priority;

  void reset();
  bool operator<(const Hand_Inout_Pattern &rhs) const;
  void print_info();
};
