#include "hand_pattern.hpp"

extern const bool console_out;

void push_back_all(std::vector<int> &v1, const std::vector<int> &v2) {
  for (int i = 0; i < v2.size(); i++) {
    v1.push_back(v2[i]);
  }
}

Hand_Pattern_Seven_Pairs::Hand_Pattern_Seven_Pairs() {}

Hand_Pattern_Seven_Pairs::Hand_Pattern_Seven_Pairs(std::vector<int> h, std::vector<int> k1,
                                                   std::vector<int> k2) {
  reset_all();
  head_num = h.size() / 2;
  for (int i = 0; i < head_num; i++) {
    for (int j = 0; j < 2; j++) {
      head[i][j] = h[i * 2 + j];
    }
  }
  k1_num = k1.size();
  for (int i = 0; i < k1_num; i++) {
    head[head_num + i][0] = k1[i];
  }
  remain = k2;
}

void Hand_Pattern_Seven_Pairs::reset_all() {
  for (int i = 0; i < 7; i++) {
    for (int j = 0; j < 2; j++) {
      head[i][j] = 0;
    }
  }
  head_num = 0;
  shanten_num = 14 - 1;
  remain.erase(remain.begin(), remain.begin() + remain.size());
}

void Hand_Pattern_Seven_Pairs::cal_shanten() {
  shanten_num = 14 - 1;
  for (int i = 0; i < 7; i++) {
    for (int j = 0; j < 2; j++) {
      if (head[i][j] != 0) {
        shanten_num--;
      }
    }
  }
}

void Hand_Pattern_Seven_Pairs::cal_tile_in_pattern() {
  std::vector<int> head_empty;
  int exists;
  for (int tile = 0; tile < 38; tile++) {
    if (tile % 10 != 0) {
      exists = 0;
      for (int i = 0; i < head_num + k1_num; i++) {
        if (tile == head[i][0]) {
          exists = 1;
        }
      }
      for (int i = 0; i < remain.size(); i++) {
        if (tile == remain[i]) {
          exists = 1;
        }
      }
      if (exists == 0) {
        head_empty.push_back(tile);
      }
    }
  }

  if (shanten_num != -1) {
    int num_empty = 7 - head_num - k1_num;
    int p = 0;
    std::vector<int> empty;
    do {
      tile_in_pattern.push_back(empty);
      for (int i = 0; i < k1_num; i++) {
        tile_in_pattern[p].push_back(head[head_num + i][0]);
      }
      for (int i = 0; i < num_empty; i++) {
        tile_in_pattern[p].push_back(head_empty[i]);
        tile_in_pattern[p].push_back(head_empty[i]);
      }
      std::sort(tile_in_pattern[p].begin(), tile_in_pattern[p].end());
      p++;
    } while (boost::next_combination(head_empty.begin(), head_empty.begin() + num_empty,
                                     head_empty.end()));
  }
}

void Hand_Pattern_Seven_Pairs::print_info() {
  if (!console_out) return;

  for (int i = 0; i < 7; i++) {
    printf("[%d,%d]", head[i][0], head[i][1]);
  }
  printf("\n");

  printf("-----tile_out---\n");
  for (int j = 0; j < remain.size(); j++) {
    printf("%d ", remain[j]);
  }
  printf("\n");

  printf("-----tile_in----\n");
  for (int i = 0; i < tile_in_pattern.size(); i++) {
    for (int j = 0; j < tile_in_pattern[i].size(); j++) {
      printf("%d ", tile_in_pattern[i][j]);
    }
    printf("\n");
  }
}

Hand_Pattern::Hand_Pattern(int pid) { my_pid = pid; }

Hand_Pattern::Hand_Pattern(int pid, const std::vector<int> &h, const std::vector<int> &m,
                           const std::vector<int> &t, const std::vector<int> &k1,
                           const std::vector<int> &k2, const Hand_State2 &hand_state,
                           int open_meld_num) {
  reset_all();
  my_pid = pid;
  for (int i = 0; i < h.size(); i++) {
    head[i] = h[i];
  }
  int meld_num = m.size() / 3;
  for (int i = 0; i < meld_num; i++) {
    for (int j = 0; j < 3; j++) {
      meld[i][j] = m[i * 3 + j];
    }
  }
  int proto_sequence_num = t.size() / 2;
  for (int i = 0; i < proto_sequence_num; i++) {
    for (int j = 0; j < 2; j++) {
      meld[meld_num + i][j] = t[i * 2 + j];
    }
  }
  for (int i = 0; i < k1.size(); i++) {
    meld[meld_num + proto_sequence_num + i][0] = k1[i];
  }
  int open_meld[4][6];
  Hand_State2 empty_hand_state;
  hand_state.get_open_meld(open_meld, empty_hand_state);
  for (int fn = 0; fn < open_meld_num; fn++) {
    meld[meld_num + proto_sequence_num + k1.size() + fn][0] = open_meld[fn][2];
    meld[meld_num + proto_sequence_num + k1.size() + fn][1] = open_meld[fn][3];
    meld[meld_num + proto_sequence_num + k1.size() + fn][2] = open_meld[fn][4];
  }
  remain = k2;
}

void Hand_Pattern::reset_all() {
  for (int i = 0; i < 2; i++) {
    head[i] = 0;
  }
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 3; j++) {
      meld[i][j] = 0;
    }
  }
  shanten_num = 12 + 2 - 1;
  remain.erase(remain.begin(), remain.end());
  tile_in_pattern.erase(tile_in_pattern.begin(), tile_in_pattern.end());
  for (int i = 0; i < 5; i++) {
    tile_in_block[i].erase(tile_in_block[i].begin(), tile_in_block[i].end());
  }
}

void Hand_Pattern::cal_shanten() {
  shanten_num = 12 + 2 - 1;
  for (int i = 0; i < 2; i++) {
    if (head[i] != 0) {
      shanten_num--;
    }
  }
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 3; j++) {
      if (meld[i][j] != 0) {
        shanten_num--;
      }
    }
  }
}

void Hand_Pattern::cal_tile_in_block() {
  std::vector<int> tmp;
  for (int mi = 0; mi < 4; mi++) {
    if (meld[mi][0] == 0) {
      for (int j = 0; j < 3; j++) {
        for (int i = 1; i <= 7; i++) {
          tmp.push_back(j * 10 + i);
          tmp.push_back(j * 10 + i + 1);
          tmp.push_back(j * 10 + i + 2);
          tile_in_block[mi].push_back(tmp);
          tmp.erase(tmp.begin(), tmp.end());
        }
        for (int i = 1; i <= 9; i++) {
          tmp.push_back(j * 10 + i);
          tmp.push_back(j * 10 + i);
          tmp.push_back(j * 10 + i);
          tile_in_block[mi].push_back(tmp);
          tmp.erase(tmp.begin(), tmp.end());
        }
      }
      for (int i = 31; i < 38; i++) {
        tmp.push_back(i);
        tmp.push_back(i);
        tmp.push_back(i);
        tile_in_block[mi].push_back(tmp);
        tmp.erase(tmp.begin(), tmp.end());
      }
    } else if (meld[mi][1] == 0) {
      if (meld[mi][0] < 30) {
        if (meld[mi][0] % 10 <= 7) {
          tmp.push_back(meld[mi][0] + 1);
          tmp.push_back(meld[mi][0] + 2);
          tile_in_block[mi].push_back(tmp);
          tmp.erase(tmp.begin(), tmp.end());
        }
        if (2 <= meld[mi][0] % 10 && meld[mi][0] % 10 <= 8) {
          tmp.push_back(meld[mi][0] - 1);
          tmp.push_back(meld[mi][0] + 1);
          tile_in_block[mi].push_back(tmp);
          tmp.erase(tmp.begin(), tmp.end());
        }
        if (3 <= meld[mi][0] % 10) {
          tmp.push_back(meld[mi][0] - 1);
          tmp.push_back(meld[mi][0] - 2);
          tile_in_block[mi].push_back(tmp);
          tmp.erase(tmp.begin(), tmp.end());
        }
      }
      tmp.push_back(meld[mi][0]);
      tmp.push_back(meld[mi][0]);
      tile_in_block[mi].push_back(tmp);
      tmp.erase(tmp.begin(), tmp.end());
    } else if (meld[mi][2] == 0) {
      if (meld[mi][0] == meld[mi][1]) {
        tmp.push_back(meld[mi][0]);
        tile_in_block[mi].push_back(tmp);
        tmp.erase(tmp.begin(), tmp.end());
      } else if (abs(meld[mi][0] - meld[mi][1]) == 2) {
        tmp.push_back((meld[mi][0] + meld[mi][1]) / 2);
        tile_in_block[mi].push_back(tmp);
        tmp.erase(tmp.begin(), tmp.end());
      } else if (meld[mi][1] - meld[mi][0] == 1) {
        if (meld[mi][0] % 10 >= 2) {
          tmp.push_back(meld[mi][0] - 1);
          tile_in_block[mi].push_back(tmp);
          tmp.erase(tmp.begin(), tmp.end());
        }
        if (meld[mi][1] % 10 <= 8) {
          tmp.push_back(meld[mi][1] + 1);
          tile_in_block[mi].push_back(tmp);
          tmp.erase(tmp.begin(), tmp.end());
        }
      }
    } else {
      tile_in_block[mi].push_back(tmp);
    }
  }

  if (head[0] == 0) {
    for (int i = 0; i < 38; i++) {
      if (i % 10 != 0) {
        tmp.push_back(i);
        tmp.push_back(i);
        tile_in_block[4].push_back(tmp);
        tmp.erase(tmp.begin(), tmp.end());
      }
    }
  } else if (head[1] == 0) {
    tmp.push_back(head[0]);
    tile_in_block[4].push_back(tmp);
    tmp.erase(tmp.begin(), tmp.end());
  } else {
    tile_in_block[4].push_back(tmp);
  }
}

int Hand_Pattern::is_in_remain(std::vector<int> test) {
  for (int i = 0; i < test.size(); i++) {
    for (int j = 0; j < remain.size(); j++) {
      if (test[i] == remain[j]) {
        return 1;
      }
    }
  }
  return 0;
}

int Hand_Pattern::check_tile_in_error(const Tile_Array &hand_kind,
                                      const Open_Meld_Vector &open_meld_kind, const int i0,
                                      const int i1, const int i2, const int i3, const int i4) {
  if (tile_in_block[0][i0].size() + tile_in_block[1][i1].size() + tile_in_block[2][i2].size() +
          tile_in_block[3][i3].size() + tile_in_block[4][i4].size() ==
      0) {
    return 1;
  }
  Tile_Array using_kind_array = using_tile_array(hand_kind, open_meld_kind);

  int tile_all[38] = {};
  for (int i = 0; i < tile_in_block[0][i0].size(); i++) {
    tile_all[tile_in_block[0][i0][i]]++;
  }
  for (int i = 0; i < tile_in_block[1][i1].size(); i++) {
    tile_all[tile_in_block[1][i1][i]]++;
  }
  for (int i = 0; i < tile_in_block[2][i2].size(); i++) {
    tile_all[tile_in_block[2][i2][i]]++;
  }
  for (int i = 0; i < tile_in_block[3][i3].size(); i++) {
    tile_all[tile_in_block[3][i3][i]]++;
  }
  for (int i = 0; i < tile_in_block[4][i4].size(); i++) {
    tile_all[tile_in_block[4][i4][i]]++;
  }
  for (int tile = 1; tile < 38; tile++) {
    if (using_kind_array[tile] + tile_all[tile] > 4) {
      return 1;
    }
  }

  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 3; j++) {
      tile_all[meld[i][j]]++;
    }
  }
  for (int j = 0; j < 2; j++) {
    tile_all[head[j]]++;
  }
  for (int tile = 1; tile < 38; tile++) {
    if (tile_all[tile] > 4) {
      return 1;
    }
  }
  return 0;
}

void Hand_Pattern::cal_tile_in_pattern(const Tile_Array &hand_kind,
                                       const Open_Meld_Vector &open_meld_kind) {
  int p = 0;
  std::vector<int> empty;
  for (int i0 = 0; i0 < tile_in_block[0].size(); i0++) {
    if (is_in_remain(tile_in_block[0][i0]) == 0 && tile_in_block[0][i0].size() != 3) {
      for (int i1 = 0; i1 < tile_in_block[1].size(); i1++) {
        if (is_in_remain(tile_in_block[1][i1]) == 0 && tile_in_block[1][i1].size() != 3) {
          for (int i2 = 0; i2 < tile_in_block[2].size(); i2++) {
            if (is_in_remain(tile_in_block[2][i2]) == 0 && tile_in_block[2][i2].size() != 3) {
              for (int i3 = 0; i3 < tile_in_block[3].size(); i3++) {
                if (is_in_remain(tile_in_block[3][i3]) == 0 && tile_in_block[3][i3].size() != 3) {
                  for (int i4 = 0; i4 < tile_in_block[4].size(); i4++) {
                    if (is_in_remain(tile_in_block[4][i4]) == 0 &&
                        (tile_in_block[4][i4].size() != 2 || shanten_num < 3)) {
                      if (check_tile_in_error(hand_kind, open_meld_kind, i0, i1, i2, i3, i4) == 0) {
                        tile_in_pattern.push_back(empty);
                        push_back_all(tile_in_pattern[p], tile_in_block[0][i0]);
                        push_back_all(tile_in_pattern[p], tile_in_block[1][i1]);
                        push_back_all(tile_in_pattern[p], tile_in_block[2][i2]);
                        push_back_all(tile_in_pattern[p], tile_in_block[3][i3]);
                        push_back_all(tile_in_pattern[p], tile_in_block[4][i4]);
                        std::sort(tile_in_pattern[p].begin(), tile_in_pattern[p].end());
                        p++;
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

int Hand_Pattern::yaku_check(int nip) {
  assert(nip < tile_in_pattern.size());
  if (half_flush_check_pattern(meld, head, tile_in_pattern[nip]) == 1) {
    return 1;
  } else if (all_triplets_check_pattern(meld, head, tile_in_pattern[nip]) == 1) {
    return 1;
  }

  return 0;
}

double Hand_Pattern::cal_priority(const Game_State &game_state, const Tile_Array &tile_visible_kind,
                                  int nip) {
  double priority = 0.0;
  if (half_flush_check_pattern(meld, head, tile_in_pattern[nip]) == 1) {
    priority += 2.0;
  }
  if (all_triplets_check_pattern(meld, head, tile_in_pattern[nip]) == 1) {
    priority += 2.0;
  }
  if (simples_check_pattern(meld, head, tile_in_pattern[nip]) == 1) {
    priority += 1.0;
  }
  priority += value_tile_check_pattern(meld, 31 + game_state.round_wind,
                                       31 + game_state.player_state[my_pid].self_wind);

  int tile_in[38] = {};
  for (int i = 0; i < tile_in_pattern[nip].size(); i++) {
    tile_in[tile_in_pattern[nip][i]]++;
  }

  double prob_score = 1.0;
  for (int tile = 0; tile < 38; tile++) {
    prob_score *= combination(4 - tile_visible_kind[tile], tile_in[tile]);
  }
  priority += prob_score / pow(4, tile_in_pattern[nip].size());

  return priority;
}

void Hand_Pattern::print_info() {
  if (!console_out) return;

  for (int i = 0; i < 4; i++) {
    printf("[%d,%d,%d]", meld[i][0], meld[i][1], meld[i][2]);
  }
  printf("[%d,%d]", head[0], head[1]);
  printf("\n");

  printf("-----tile_out---\n");
  for (int j = 0; j < remain.size(); j++) {
    printf("%d ", remain[j]);
  }
  printf("\n");

  printf("-----tile_in----\n");
  for (int i = 0; i < tile_in_pattern.size(); i++) {
    for (int j = 0; j < tile_in_pattern[i].size(); j++) {
      printf("%d ", tile_in_pattern[i][j]);
    }
    printf(",\n");
  }
}
