#include "bit_tile_count.hpp"

extern const bool console_out;

uint32_t bit_to_num(uint32_t bit, int x) { return (bit >> (3 * (x - 1))) & 7; }

Bit_Tile_Num::Bit_Tile_Num() { reset(); }

void Bit_Tile_Num::reset() {
  for (int i = 0; i < 4; i++) {
    color[i] = 0;
  }
  size = 0;
}

Bit_Tile_Num &Bit_Tile_Num::operator=(const Bit_Tile_Num &rhs) {
  if (this != &rhs) {
    for (int c = 0; c < 4; c++) {
      color[c] = rhs.color[c];
    }
    size = rhs.size;
  }
  return *this;
}

bool Bit_Tile_Num::operator==(const Bit_Tile_Num &rhs) const {
  for (int i = 0; i < 4; i++) {
    if (color[i] != rhs.color[i]) {
      return false;
    }
  }
  return true;
}

bool Bit_Tile_Num::operator!=(const Bit_Tile_Num &rhs) const { return !(*this == rhs); }

int Bit_Tile_Num::get_size() const { return size; }

int Bit_Tile_Num::count_tile(const int tile) const {
  assert(tile % 10 != 0);
  return bit_to_num(color[tile / 10], tile % 10);
}

void Bit_Tile_Num::add_tile(const int tile) {
  assert(tile % 10 != 0);
  color[tile / 10] += 1 << ((tile % 10 - 1) * 3);
  size++;
}

void Bit_Tile_Num::delete_tile(const int tile) {
  assert(tile % 10 != 0);
  color[tile / 10] -= 1 << ((tile % 10 - 1) * 3);
  size--;
}

void Bit_Tile_Num::set_from_array38(const int hand[38]) {
  reset();
  for (int i = 0; i < 3; i++) {
    for (int j = 1; j <= 9; j++) {
      color[i] += hand[10 * i + j] << ((j - 1) * 3);
      size += hand[10 * i + j];
    }
    color[i] += hand[10 * i + 10] << ((5 - 1) * 3);
    size += hand[10 * i + 10];
  }
  for (int j = 1; j <= 7; j++) {
    color[3] += hand[30 + j] << ((j - 1) * 3);
    size += hand[30 + j];
  }
}

void Bit_Tile_Num::insert_to_array38(Tile_Array &hand) const {
  hand[0] = 0;
  for (int c = 0; c < 3; c++) {
    for (int j = 1; j <= 9; j++) {
      hand[10 * c + j] = count_tile(10 * c + j);
    }
    hand[10 * c + 10] = 0;
  }
  for (int j = 1; j <= 7; j++) {
    hand[30 + j] = count_tile(30 + j);
  }
}

void Bit_Tile_Num::print_info() const {
  if (!console_out) return;

  for (int tile = 0; tile < 38; tile++) {
    if (tile % 10 != 0) {
      for (int i = 0; i < count_tile(tile); i++) {
        std::cout << tile << " ";
      }
    }
  }
  std::cout << std::endl;
}

size_t Bit_Tile_Num::hash_value() const {
  size_t res = 0;
  for (int i = 0; i < 4; i++) {
    res ^= color[i] * (i + 1);
  }
  return res;
}

size_t hash_value(const Bit_Tile_Num &bit_tile_num) { return bit_tile_num.hash_value(); }
