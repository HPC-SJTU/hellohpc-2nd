#pragma once

#include "../share/include.hpp"
#include "../share/types.hpp"

uint32_t bit_to_num(uint32_t bit, int x);

class Bit_Tile_Num {
 private:
 public:
  uint32_t color[4];
  int size;
  Bit_Tile_Num &operator=(const Bit_Tile_Num &rhs);
  bool operator==(const Bit_Tile_Num &rhs) const;
  bool operator!=(const Bit_Tile_Num &rhs) const;
  Bit_Tile_Num();
  void reset();

  int get_size() const;
  int count_tile(const int tile) const;

  void add_tile(const int tile);
  void delete_tile(const int tile);

  void set_from_array38(const int hand[38]);
  void insert_to_array38(Tile_Array &hand) const;

  void print_info() const;
  size_t hash_value() const;
};

size_t hash_value(const Bit_Tile_Num &bit_tile_num);
