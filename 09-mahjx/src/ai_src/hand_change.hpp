#pragma once

#include "../share/include.hpp"
#include "bit_tile_count.hpp"
#include "hand_analyzer.hpp"

class Hand_Change {
 public:
  Hand_Change();
  void reset();

  Bit_Tile_Num hand_base;
  int id;
  Hand_Change &operator=(const Hand_Change &rhs);
  bool operator==(const Hand_Change &rhs) const;

  size_t hash_value() const;
  void print_info() const;
};

size_t hash_value(const Hand_Change &tc);
