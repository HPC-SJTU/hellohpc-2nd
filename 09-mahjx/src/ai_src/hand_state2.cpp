#include "hand_state2.hpp"

extern const bool console_out;

Hand_State2::Hand_State2() { reset(); }

Hand_State2::Hand_State2(const Tile_Array &hand, const bool rf, const Open_Meld_Vector &open_meld) {
  reset_with(hand, rf, open_meld);
}

Hand_State2 &Hand_State2::operator=(const Hand_State2 &rhs) {
  if (this != &rhs) {
    for (int i = 0; i < 3; i++) {
      chii[i] = rhs.chii[i];
    }
    for (int i = 0; i < 4; i++) {
      pon_kan[i] = rhs.pon_kan[i];
    }
    riichi_red = rhs.riichi_red;
  }
  return *this;
}

bool Hand_State2::operator==(const Hand_State2 &rhs) const {
  for (int i = 0; i < 3; i++) {
    if (chii[i] != rhs.chii[i]) {
      return false;
    }
  }
  for (int i = 0; i < 4; i++) {
    if (pon_kan[i] != rhs.pon_kan[i]) {
      return false;
    }
  }
  if (riichi_red != rhs.riichi_red) {
    return false;
  }
  return true;
}

bool Hand_State2::operator!=(const Hand_State2 &rhs) const { return !(*this == rhs); }

void Hand_State2::reset() {
  for (int c = 0; c < 3; c++) {
    chii[c] = 0;
    set_red_inside(c, 0);
    set_red_outside(c, 0);
  }
  for (int c = 0; c < 4; c++) {
    pon_kan[c] = 0;
  }
  set_riichi(0);
}

void Hand_State2::reset_open_meld() {
  for (int c = 0; c < 3; c++) {
    chii[c] = 0;
    set_red_outside(c, 0);
  }
  for (int c = 0; c < 4; c++) {
    pon_kan[c] = 0;
  }
}

void Hand_State2::set_open_meld(const Open_Meld_Vector &open_meld) {
  reset_open_meld();
  for (const auto &elem : open_meld) {
    if (elem.type != FT_CONCEALED_KAN) {
      if (elem.tile % 10 == 0 && 10 <= elem.tile) {
        set_red_outside(elem.tile / 10 - 1, 1);
      }
    }
    for (int j = 0; j < elem.consumed.size(); j++) {
      if (elem.consumed[j] % 10 == 0 && 10 <= elem.consumed[j]) {
        set_red_outside(elem.consumed[j] / 10 - 1, 1);
      }
    }
    if (elem.type == FT_CHII) {
      add_one_open_meld(elem.type, std::min({tile_kind(elem.consumed[0]),
                                             tile_kind(elem.consumed[1]), tile_kind(elem.tile)}));
    } else {
      add_one_open_meld(elem.type, tile_kind(elem.consumed[0]));
    }
  }
}

void Hand_State2::reset_with(const Tile_Array &hand, const bool rf,
                             const Open_Meld_Vector &open_meld) {
  for (int i = 0; i < 3; i++) {
    chii[i] = 0;
  }
  for (int i = 0; i < 4; i++) {
    pon_kan[i] = 0;
  }
  riichi_red = 0;

  if (rf) {
    riichi_red += 1;
  }
  for (int hc = 0; hc < 3; hc++) {
    if (hand[10 * (hc + 1)] == 1) {
      riichi_red += 1 << (hc + 1);
    }
  }
  set_open_meld(open_meld);
}

void Hand_State2::set_red_inside(int color, int flag) {
  uint32_t bit = 1 << (color + 1);
  if (flag == 1) {
    riichi_red = riichi_red | bit;
  } else {
    riichi_red = riichi_red & (~bit);
  }
}

void Hand_State2::set_red_outside(int color, int flag) {
  uint32_t bit = 1 << (color + 4);
  if (flag == 1) {
    riichi_red = riichi_red | bit;
  } else {
    riichi_red = riichi_red & (~bit);
  }
}

void Hand_State2::set_riichi(int flag) {
  uint32_t bit = 1;
  if (flag == 1) {
    riichi_red = riichi_red | bit;
  } else {
    riichi_red = riichi_red & (~bit);
  }
}

void Hand_State2::add_chii(int smallest_tile_kind) {
  chii[smallest_tile_kind / 10] += 1 << (3 * (smallest_tile_kind % 10 - 1));
}

void Hand_State2::delete_chii(int smallest_tile_kind) {
  chii[smallest_tile_kind / 10] -= 1 << (3 * (smallest_tile_kind % 10 - 1));
}

void Hand_State2::add_pon(int pon_tile_kind) {
  pon_kan[pon_tile_kind / 10] += 1 << (3 * (pon_tile_kind % 10 - 1));
}

void Hand_State2::delete_pon(int pon_tile_kind) {
  pon_kan[pon_tile_kind / 10] -= 1 << (3 * (pon_tile_kind % 10 - 1));
}

void Hand_State2::add_concealed_kan(int concealed_kan_tile_kind) {
  pon_kan[concealed_kan_tile_kind / 10] += 1 << (3 * (concealed_kan_tile_kind % 10 - 1) + 1);
}

void Hand_State2::delete_concealed_kan(int concealed_kan_tile_kind) {
  pon_kan[concealed_kan_tile_kind / 10] -= 1 << (3 * (concealed_kan_tile_kind % 10 - 1) + 1);
}

void Hand_State2::add_open_kan(int open_kan_tile_kind) {
  pon_kan[open_kan_tile_kind / 10] += 1 << (3 * (open_kan_tile_kind % 10 - 1) + 2);
}

void Hand_State2::delete_open_kan(int open_kan_tile_kind) {
  pon_kan[open_kan_tile_kind / 10] -= 1 << (3 * (open_kan_tile_kind % 10 - 1) + 2);
}

void Hand_State2::add_one_open_meld(const Open_Meld_Type open_meld_type,
                                    const int smallest_tile_kind) {
  if (open_meld_type == FT_CHII) {
    add_chii(smallest_tile_kind);
  } else if (open_meld_type == FT_PON) {
    add_pon(smallest_tile_kind);
  } else if (open_meld_type == FT_CONCEALED_KAN) {
    add_concealed_kan(smallest_tile_kind);
  } else if (open_meld_type == FT_OPEN_KAN || open_meld_type == FT_UPGRADED_KAN) {
    add_open_kan(smallest_tile_kind);
  }
}

void Hand_State2::delete_one_open_meld(const Open_Meld_Type open_meld_type,
                                       int smallest_tile_kind) {
  if (open_meld_type == FT_CHII) {
    delete_chii(smallest_tile_kind);
  } else if (open_meld_type == FT_PON) {
    delete_pon(smallest_tile_kind);
  } else if (open_meld_type == FT_CONCEALED_KAN) {
    delete_concealed_kan(smallest_tile_kind);
  } else if (open_meld_type == FT_OPEN_KAN || open_meld_type == FT_UPGRADED_KAN) {
    delete_open_kan(smallest_tile_kind);
  }
}

int Hand_State2::get_riichi() const { return riichi_red & 1; }

int Hand_State2::get_chii_num(const int smallest_tile_kind) const {
  return (chii[smallest_tile_kind / 10] >> (3 * (smallest_tile_kind % 10 - 1))) & 7;
}

int Hand_State2::get_pon_num(const int tile_kind) const {
  return (pon_kan[tile_kind / 10] >> (3 * (tile_kind % 10 - 1))) & 1;
}

int Hand_State2::get_concealed_kan_num(const int tile_kind) const {
  return (pon_kan[tile_kind / 10] >> (3 * (tile_kind % 10 - 1) + 1)) & 1;
}

int Hand_State2::get_open_kan_num(const int tile_kind) const {
  return (pon_kan[tile_kind / 10] >> (3 * (tile_kind % 10 - 1) + 2)) & 1;
}

int Hand_State2::get_red_inside(const int color) const { return (riichi_red >> (color + 1)) & 1; }

int Hand_State2::get_red_outside(const int color) const { return (riichi_red >> (color + 4)) & 1; }

int Hand_State2::get_tile_kind_num(const int tile_kind) const {
  assert(tile_kind % 10 != 0);
  int res = 0;
  res += 3 * get_pon_num(tile_kind);
  res += 4 * get_open_kan_num(tile_kind);
  res += 4 * get_concealed_kan_num(tile_kind);
  if (tile_kind < 30) {
    if (tile_kind % 10 <= 7) {
      res += get_chii_num(tile_kind);
    }
    if (2 <= tile_kind % 10 && tile_kind % 10 <= 8) {
      res += get_chii_num(tile_kind - 1);
    }
    if (3 <= tile_kind % 10) {
      res += get_chii_num(tile_kind - 2);
    }
  }
  return res;
}

Open_Meld_Vector Hand_State2::get_open_meld(const Hand_State2 &present_hand_state) const {
  Open_Meld_Vector open_meld;
  for (int c = 0; c < 3; c++) {
    for (int j = 1; j <= 7; j++) {
      for (int n = 0; n < present_hand_state.get_chii_num(c * 10 + j); n++) {
        Open_Meld_Elem elem;
        elem.type = FT_CHII;
        elem.tile = c * 10 + j;
        elem.consumed.push_back(c * 10 + j + 1);
        elem.consumed.push_back(c * 10 + j + 2);
        open_meld.push_back(elem);
      }
    }
  }
  for (int tile = 0; tile < 38; tile++) {
    if (tile % 10 != 0) {
      for (int n = 0; n < present_hand_state.get_pon_num(tile); n++) {
        Open_Meld_Elem elem;
        elem.type = FT_PON;
        elem.tile = tile;
        elem.consumed.push_back(tile);
        elem.consumed.push_back(tile);
        open_meld.push_back(elem);
      }
      for (int n = 0; n < present_hand_state.get_concealed_kan_num(tile); n++) {
        Open_Meld_Elem elem;
        elem.type = FT_CONCEALED_KAN;
        elem.consumed.push_back(tile);
        elem.consumed.push_back(tile);
        elem.consumed.push_back(tile);
        elem.consumed.push_back(tile);
        open_meld.push_back(elem);
      }
      for (int n = 0; n < present_hand_state.get_open_kan_num(tile); n++) {
        Open_Meld_Elem elem;
        elem.type = FT_OPEN_KAN;
        elem.tile = tile;
        elem.consumed.push_back(tile);
        elem.consumed.push_back(tile);
        elem.consumed.push_back(tile);
        open_meld.push_back(elem);
      }
    }
  }
  for (int c = 0; c < 3; c++) {
    if (present_hand_state.get_red_outside(c) == 1) {
      int flag = 0;
      for (int fn = 0; fn < open_meld.size(); fn++) {
        if (open_meld[fn].type != FT_CONCEALED_KAN && open_meld[fn].tile == c * 10 + 5 &&
            flag == 0) {
          open_meld[fn].tile += 5;
          flag = 1;
        }
        for (int i = 0; i < open_meld[fn].consumed.size(); i++) {
          if (open_meld[fn].consumed[i] == c * 10 + 5 && flag == 0) {
            open_meld[fn].consumed[i] += 5;
            flag = 1;
          }
        }
      }
      assert(flag == 1);
    }
  }

  const int present_open_meld_num = open_meld.size();
  for (int c = 0; c < 3; c++) {
    for (int j = 1; j <= 7; j++) {
      for (int n = 0; n < get_chii_num(c * 10 + j) - present_hand_state.get_chii_num(c * 10 + j);
           n++) {
        Open_Meld_Elem elem;
        elem.type = FT_CHII;
        elem.tile = c * 10 + j;
        elem.consumed.push_back(c * 10 + j + 1);
        elem.consumed.push_back(c * 10 + j + 2);
        open_meld.push_back(elem);
      }
    }
  }
  for (int tile = 0; tile < 38; tile++) {
    if (tile % 10 != 0) {
      for (int n = 0; n < get_pon_num(tile) - present_hand_state.get_pon_num(tile); n++) {
        Open_Meld_Elem elem;
        elem.type = FT_PON;
        elem.tile = tile;
        elem.consumed.push_back(tile);
        elem.consumed.push_back(tile);
        open_meld.push_back(elem);
      }
      for (int n = 0;
           n < get_concealed_kan_num(tile) - present_hand_state.get_concealed_kan_num(tile); n++) {
        Open_Meld_Elem elem;
        elem.type = FT_CONCEALED_KAN;
        elem.consumed.push_back(tile);
        elem.consumed.push_back(tile);
        elem.consumed.push_back(tile);
        elem.consumed.push_back(tile);
        open_meld.push_back(elem);
      }
      for (int n = 0; n < get_open_kan_num(tile) - present_hand_state.get_open_kan_num(tile); n++) {
        Open_Meld_Elem elem;
        elem.type = FT_OPEN_KAN;
        elem.tile = tile;
        elem.consumed.push_back(tile);
        elem.consumed.push_back(tile);
        elem.consumed.push_back(tile);
        open_meld.push_back(elem);
      }
    }
  }

  for (int c = 0; c < 3; c++) {
    if (get_red_outside(c) - present_hand_state.get_red_outside(c) == 1) {
      int flag = 0;
      for (int fn = present_open_meld_num; fn < open_meld.size(); fn++) {
        if (open_meld[fn].type != FT_CONCEALED_KAN && open_meld[fn].tile == c * 10 + 5 &&
            flag == 0) {
          open_meld[fn].tile += 5;
          flag = 1;
        }
        for (int i = 0; i < open_meld[fn].consumed.size(); i++) {
          if (open_meld[fn].consumed[i] == c * 10 + 5 && flag == 0) {
            open_meld[fn].consumed[i] += 5;
            flag = 1;
          }
        }
      }
      assert(flag == 1);
    }
  }
  return open_meld;
}

void Hand_State2::get_open_meld(int open_meld[4][6], const Hand_State2 &present_hand_state) const {
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 6; j++) {
      open_meld[i][j] = 0;
    }
  }
  int open_meld_num = 0;
  for (int c = 0; c < 3; c++) {
    for (int j = 1; j <= 7; j++) {
      for (int n = 0; n < present_hand_state.get_chii_num(c * 10 + j); n++) {
        open_meld[open_meld_num][0] = 1;
        open_meld[open_meld_num][2] = c * 10 + j;
        open_meld[open_meld_num][3] = c * 10 + j + 1;
        open_meld[open_meld_num][4] = c * 10 + j + 2;
        open_meld_num++;
      }
    }
  }
  for (int tile = 0; tile < 38; tile++) {
    if (tile % 10 != 0) {
      for (int n = 0; n < present_hand_state.get_pon_num(tile); n++) {
        open_meld[open_meld_num][0] = 2;
        open_meld[open_meld_num][2] = tile;
        open_meld[open_meld_num][3] = tile;
        open_meld[open_meld_num][4] = tile;
        open_meld_num++;
      }
      for (int n = 0; n < present_hand_state.get_concealed_kan_num(tile); n++) {
        open_meld[open_meld_num][0] = 4;
        open_meld[open_meld_num][2] = tile;
        open_meld[open_meld_num][3] = tile;
        open_meld[open_meld_num][4] = tile;
        open_meld[open_meld_num][5] = tile;
        open_meld_num++;
      }
      for (int n = 0; n < present_hand_state.get_open_kan_num(tile); n++) {
        open_meld[open_meld_num][0] = 3;
        open_meld[open_meld_num][2] = tile;
        open_meld[open_meld_num][3] = tile;
        open_meld[open_meld_num][4] = tile;
        open_meld[open_meld_num][5] = tile;
        open_meld_num++;
      }
    }
  }
  for (int c = 0; c < 3; c++) {
    if (present_hand_state.get_red_outside(c) == 1) {
      int flag = 0;
      for (int fn = 0; fn < open_meld_num; fn++) {
        for (int i = 2; i < 6; i++) {
          if (open_meld[fn][i] == c * 10 + 5 && flag == 0) {
            open_meld[fn][i] += 5;
            flag = 1;
          }
        }
      }
      assert(flag == 1);
    }
  }

  int present_open_meld_num = open_meld_num;
  for (int c = 0; c < 3; c++) {
    for (int j = 1; j <= 7; j++) {
      for (int n = 0; n < get_chii_num(c * 10 + j) - present_hand_state.get_chii_num(c * 10 + j);
           n++) {
        open_meld[open_meld_num][0] = 1;
        open_meld[open_meld_num][2] = c * 10 + j;
        open_meld[open_meld_num][3] = c * 10 + j + 1;
        open_meld[open_meld_num][4] = c * 10 + j + 2;
        open_meld_num++;
      }
    }
  }
  for (int tile = 0; tile < 38; tile++) {
    if (tile % 10 != 0) {
      for (int n = 0; n < get_pon_num(tile) - present_hand_state.get_pon_num(tile); n++) {
        open_meld[open_meld_num][0] = 2;
        open_meld[open_meld_num][2] = tile;
        open_meld[open_meld_num][3] = tile;
        open_meld[open_meld_num][4] = tile;
        open_meld_num++;
      }
      for (int n = 0;
           n < get_concealed_kan_num(tile) - present_hand_state.get_concealed_kan_num(tile); n++) {
        open_meld[open_meld_num][0] = 4;
        open_meld[open_meld_num][2] = tile;
        open_meld[open_meld_num][3] = tile;
        open_meld[open_meld_num][4] = tile;
        open_meld[open_meld_num][5] = tile;
        open_meld_num++;
      }
      for (int n = 0; n < get_open_kan_num(tile) - present_hand_state.get_open_kan_num(tile); n++) {
        open_meld[open_meld_num][0] = 3;
        open_meld[open_meld_num][2] = tile;
        open_meld[open_meld_num][3] = tile;
        open_meld[open_meld_num][4] = tile;
        open_meld[open_meld_num][5] = tile;
        open_meld_num++;
      }
    }
  }

  for (int c = 0; c < 3; c++) {
    if (get_red_outside(c) - present_hand_state.get_red_outside(c) == 1) {
      int flag = 0;
      for (int fn = present_open_meld_num; fn < open_meld_num; fn++) {
        for (int i = 2; i < 6; i++) {
          if (open_meld[fn][i] == c * 10 + 5 && flag == 0) {
            open_meld[fn][i] += 5;
            flag = 1;
          }
        }
      }
      assert(flag == 1);
    }
  }
}

void Hand_State2::print_info() {
  if (!console_out) return;

  std::cout << "chii:";
  for (int i = 0; i < 3; i++) {
    std::cout << chii[i] << " ";
  }
  std::cout << std::endl;
  std::cout << "pon_kan:";
  for (int i = 0; i < 4; i++) {
    std::cout << pon_kan[i] << " ";
  }
  std::cout << std::endl;
  std::cout << "riichi_red:" << riichi_red << std::endl;
}

size_t Hand_State2::hash_value() const {
  size_t res = riichi_red;
  for (int i = 0; i < 3; i++) {
    res ^= chii[i];
  }
  for (int i = 0; i < 4; i++) {
    res ^= pon_kan[i];
  }
  return res;
}

size_t hash_value(const Hand_State2 &hand_state2) { return hand_state2.hash_value(); }

bool is_same_open_meld(const Hand_State2 &ts1, const Hand_State2 &ts2) {
  for (int tile = 1; tile < 38; tile++) {
    if (tile % 10 != 0) {
      if (ts1.get_pon_num(tile) != ts2.get_pon_num(tile)) {
        return false;
      }
      if (ts1.get_open_kan_num(tile) != ts2.get_open_kan_num(tile)) {
        return false;
      }
      if (ts1.get_concealed_kan_num(tile) != ts2.get_concealed_kan_num(tile)) {
        return false;
      }
      if (tile % 10 <= 7 && ts1.get_chii_num(tile) != ts2.get_chii_num(tile)) {
        return false;
      }
    }
  }
  for (int c = 0; c < 3; c++) {
    if (ts1.get_red_outside(c) != ts2.get_red_outside(c)) {
      return false;
    }
  }
  return true;
}

bool is_ts_open_meld_consistent(const Hand_State2 &hand_state, const Open_Meld_Vector &open_meld) {
  Hand_State2 ts;
  ts.set_open_meld(open_meld);
  return is_same_open_meld(hand_state, ts);
}
