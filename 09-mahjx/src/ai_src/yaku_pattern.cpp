#include "yaku_pattern.hpp"

int value_tile_check_pattern(int meld[4][3], int round_wind_tile, int self_wind_tile) {
  int res = 0;
  for (int i = 0; i < 4; i++) {
    if (35 <= meld[i][0] && meld[i][0] <= 37) {
      res += 1;
    }
    if (meld[i][0] == round_wind_tile) {
      res += 1;
    }
    if (meld[i][0] == self_wind_tile) {
      res += 1;
    }
  }
  return res;
}

int full_flush_check_pattern(int meld[4][3], int head[2], std::vector<int> tile_in) {
  int flags[3];
  for (int i = 0; i < 3; i++) {
    flags[i] = 1;
  }
  for (Color_Type color = CT_CHARACTERS; color <= CT_BAMBOO; ++color) {
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 3; j++) {
        if (meld[i][j] == 0) {
          break;
        } else if (tile_color(meld[i][j]) != color) {
          flags[color] = 0;
          break;
        }
      }
    }
    for (int i = 0; i < 2; i++) {
      if (head[i] == 0) {
        break;
      } else if (tile_color(head[i]) != color) {
        flags[color] = 0;
        break;
      }
    }
    for (int i = 0; i < tile_in.size(); i++) {
      if (tile_color(tile_in[i]) != color) {
        flags[color] = 0;
        break;
      }
    }
  }
  return flags[0] + flags[1] + flags[2];
}

int half_flush_check_pattern(int meld[4][3], int head[2], std::vector<int> tile_in) {
  int flags[3];
  for (int i = 0; i < 3; i++) {
    flags[i] = 1;
  }
  for (Color_Type color = CT_CHARACTERS; color <= CT_BAMBOO; ++color) {
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 3; j++) {
        if (meld[i][j] == 0) {
          break;
        } else if (tile_color(meld[i][j]) != color && tile_color(meld[i][j]) != 3) {
          flags[color] = 0;
          break;
        }
      }
    }
    for (int i = 0; i < 2; i++) {
      if (head[i] == 0) {
        break;
      } else if (tile_color(head[i]) != color && tile_color(head[i]) != 3) {
        flags[color] = 0;
        break;
      }
    }
    for (int i = 0; i < tile_in.size(); i++) {
      if (tile_color(tile_in[i]) != color && tile_color(tile_in[i]) != 3) {
        flags[color] = 0;
        break;
      }
    }
  }
  return flags[0] + flags[1] + flags[2];
}

int simples_check_pattern(int meld[4][3], int head[2], std::vector<int> tile_in) {
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 3; j++) {
      if (meld[i][j] == 0) {
        break;
      } else if (tile_terminal_or_honor(meld[i][j]) != 0) {
        return 0;
      }
    }
  }
  for (int i = 0; i < 2; i++) {
    if (head[i] == 0) {
      break;
    } else if (tile_terminal_or_honor(head[i]) != 0) {
      return 0;
    }
  }
  for (int i = 0; i < tile_in.size(); i++) {
    if (tile_terminal_or_honor(tile_in[i]) != 0) {
      return 0;
    }
  }
  return 1;
}

int all_triplets_check_pattern(int meld[4][3], int head[2], std::vector<int> tile_in) {
  int tile_present[38] = {};
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 3; j++) {
      tile_present[meld[i][j]] = 1;
    }
  }
  for (int i = 0; i < 2; i++) {
    tile_present[head[i]] = 1;
  }
  for (int i = 0; i < tile_in.size(); i++) {
    tile_present[tile_in[i]] = 1;
  }
  int counter = 0;
  for (int tile = 1; tile < 38; tile++) {
    if (tile_present[tile] == 1) {
      counter++;
    }
  }
  if (counter == 5) {
    return 1;
  } else {
    return 0;
  }
}
