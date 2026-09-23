#include "hand_change.hpp"

extern const bool console_out;

Hand_Change::Hand_Change() { reset(); }

void Hand_Change::reset() { hand_base.reset(); }

Hand_Change &Hand_Change::operator=(const Hand_Change &rhs) {
  if (this != &rhs) {
    hand_base = rhs.hand_base;
    id = rhs.id;
  }
  return *this;
}

bool Hand_Change::operator==(const Hand_Change &rhs) const {
  for (int c = 0; c < 4; c++) {
    if (hand_base.color[c] != rhs.hand_base.color[c]) {
      return false;
    }
  }
  return true;
}

void Hand_Change::print_info() const {
  if (!console_out) return;

  std::cout << "id:" << id << std::endl;
  hand_base.print_info();
}

size_t Hand_Change::hash_value() const { return hand_base.hash_value(); }

size_t hash_value(const Hand_Change &tc) { return tc.hash_value(); }
