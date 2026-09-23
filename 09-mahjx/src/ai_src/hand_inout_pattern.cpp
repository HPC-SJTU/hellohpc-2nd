#include "hand_inout_pattern.hpp"

extern const bool console_out;

Hand_Inout_Pattern::Hand_Inout_Pattern() { reset(); }

void Hand_Inout_Pattern::reset() {
  tile_in_pattern.erase(tile_in_pattern.begin(), tile_in_pattern.end());
  tile_out_pattern.erase(tile_out_pattern.begin(), tile_out_pattern.end());
  priority = 0.0;
}

bool Hand_Inout_Pattern::operator<(const Hand_Inout_Pattern &rhs) const {
  if (priority < rhs.priority) {
    return true;
  } else {
    return false;
  }
}

void Hand_Inout_Pattern::print_info() {
  if (!console_out) return;

  std::cout << "tile_in:";
  for (int i = 0; i < tile_in_pattern.size(); i++) {
    std::cout << tile_in_pattern[i] << " ";
  }
  std::cout << std::endl;
  std::cout << "tile_out:";
  for (int i = 0; i < tile_out_pattern.size(); i++) {
    std::cout << tile_out_pattern[i] << " ";
  }
  std::cout << std::endl;
  std::cout << "priority:" << priority << std::endl;
}
