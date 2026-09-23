#include "hand_analyzer.hpp"

#include "../share/riichi_rules.hpp"

extern const bool console_out;

void copy_hand(const int hand_src[38], int hand_dst[38]) {
  for (int i = 0; i < 38; i++) {
    hand_dst[i] = hand_src[i];
  }
}

void get_hand_kind_counts(const int hand_src[38], int hand_kind_counts[38]) {
  copy_hand(hand_src, hand_kind_counts);
  for (int i = 1; i <= 3; i++) {
    if (hand_kind_counts[i * 10] == 1) {
      hand_kind_counts[i * 10 - 5]++;
      hand_kind_counts[i * 10] = 0;
    }
  }
}

Hand_Pattern_Source::Hand_Pattern_Source() { reset(); }

void Hand_Pattern_Source::reset() {
  meld_tile.erase(meld_tile.begin(), meld_tile.end());
  head_tile.erase(head_tile.begin(), head_tile.end());
  proto_sequence_tile.erase(proto_sequence_tile.begin(), proto_sequence_tile.end());
  isolated_tile_1.erase(isolated_tile_1.begin(), isolated_tile_1.end());
  isolated_tile_2.erase(isolated_tile_2.begin(), isolated_tile_2.end());
}

Hand_Analyzer_Basic::Hand_Analyzer_Basic() { reset_hand_analyzer_basic(); }

Hand_Analyzer::Hand_Analyzer() : Hand_Analyzer_Basic() {}

void Hand_Analyzer_Basic::reset_tenpai() {
  set_tenpai(0);
  set_furiten(0);
  set_meld_shanten_num(8);
  set_seven_pairs_shanten_num(6);
  set_win_shanten_num(8);
  // Do not set the riichi flag to 0 here. reset_with sets the riichi flag. We set it to 0 before,
  // so check for side effects.
}

void Hand_Analyzer_Basic::reset_hand_analyzer_basic() {
  set_negative(0);
  set_kan_changed(0);
  set_hand_num(0);

  set_open_meld_num(0);
  set_concealed_kan_num(0);

  reset_tenpai();
  hand_state.reset();
}

void Hand_Analyzer_Basic::reset_hand_analyzer_basic_with(const Tile_Array &hand_src,
                                                         const bool is_riichi,
                                                         const Open_Meld_Vector &open_meld) {
  reset_hand_analyzer_basic();
  set_hand_num(0);
  for (int tile = 1; tile < 38; tile++) {
    for (int i = 0; i < hand_src[tile]; i++) {
      hand_bit.add_tile(tile_kind(tile));
      add_hand_num(1);
    }
  }
  set_open_meld(open_meld);
  hand_state.reset_with(hand_src, is_riichi, open_meld);
}

void Hand_Analyzer_Basic::reset_hand_analyzer_basic_with2(const Bit_Tile_Num &hand_bit_src,
                                                          const Hand_State2 &ts) {
  reset_hand_analyzer_basic();
  hand_bit = hand_bit_src;
  set_hand_num(hand_bit.get_size());

  Hand_State2 hand_state_empty;
  int open_meld[4][6];
  ts.get_open_meld(open_meld, hand_state_empty);
  for (int i = 0; i < 4; i++) {
    if (open_meld[i][0] != 0) {
      add_open_meld_num(1);
    }
    if (open_meld[i][0] == 4) {
      add_concealed_kan_num(1);
    }
  }
  hand_state = ts;
}

void Hand_Analyzer::reset_hand_analyzer() {
  reset_hand_analyzer_basic();
  pattern = 0;
  meld_change_num_max = 0;
  seven_pairs_change_num_max = 0;
  for (int i = 0; i < 9; i++) {
    inout_pattern_vec[i].erase(inout_pattern_vec[i].begin(), inout_pattern_vec[i].end());
  }
  for (int i = 0; i < 7; i++) {
    pattern_seven_pairs_vec[i].erase(pattern_seven_pairs_vec[i].begin(),
                                     pattern_seven_pairs_vec[i].end());
  }
}

void Hand_Analyzer::reset_hand_analyzer_with(const Tile_Array &hand_src, const bool is_riichi,
                                             const Open_Meld_Vector &open_meld) {
  reset_hand_analyzer();
  reset_hand_analyzer_basic_with(hand_src, is_riichi, open_meld);
}

bool Hand_Analyzer_Basic::operator==(const Hand_Analyzer_Basic &rhs) const {
  if (hand_bit != rhs.hand_bit) {
    return false;
  }
  // 2017/11/17: we added the hand_state equality condition below. We think the discards so far were
  // fine without it. We must examine whether the hand_state equality check changes the behavior.
  if (hand_state != rhs.hand_state) {
    return false;
  }
  return true;
}

bool Hand_Analyzer_Basic::operator!=(const Hand_Analyzer_Basic &rhs) const {
  return !(*this == rhs);
}

void Hand_Analyzer_Basic::set_riichi(int flag) { hand_state.set_riichi(flag); }

int Hand_Analyzer_Basic::get_riichi() const { return hand_state.get_riichi(); }

int Hand_Analyzer_Basic::get_tenpai() const { return num_and_flags & 1; }

int Hand_Analyzer_Basic::get_furiten() const { return (num_and_flags >> 1) & 1; }

int Hand_Analyzer_Basic::get_negative() const { return (num_and_flags >> 2) & 1; }

int Hand_Analyzer_Basic::get_kan_changed() const { return (num_and_flags >> 3) & 1; }

void Hand_Analyzer_Basic::set_tenpai(const int flag) {
  uint32_t bit = 1;
  if (flag == 1) {
    num_and_flags = num_and_flags | bit;
  } else {
    num_and_flags = num_and_flags & (~bit);
  }
}

void Hand_Analyzer_Basic::set_furiten(const int flag) {
  uint32_t bit = 1 << 1;
  if (flag == 1) {
    num_and_flags = num_and_flags | bit;
  } else {
    num_and_flags = num_and_flags & (~bit);
  }
}

void Hand_Analyzer_Basic::set_negative(const int flag) {
  uint32_t bit = 1 << 2;
  if (flag == 1) {
    num_and_flags = num_and_flags | bit;
  } else {
    num_and_flags = num_and_flags & (~bit);
  }
}

void Hand_Analyzer_Basic::set_kan_changed(const int flag) {
  uint32_t bit = 1 << 3;
  if (flag == 1) {
    num_and_flags = num_and_flags | bit;
  } else {
    num_and_flags = num_and_flags & (~bit);
  }
}

int Hand_Analyzer_Basic::get_hand_num() const { return (num_and_flags & 0x000000F0) >> 4; }

void Hand_Analyzer_Basic::set_hand_num(const int num) {
  assert(num <= 15);  // To get the effective tiles, we sometimes add one tile to a 14-tile hand.
  num_and_flags = (num_and_flags & 0xFFFFFF0F) + (uint32_t(num) << 4);
}

void Hand_Analyzer_Basic::add_hand_num(const int num) { num_and_flags += (uint32_t(num) << 4); }

void Hand_Analyzer_Basic::reduce_hand_num(const int num) { num_and_flags -= (uint32_t(num) << 4); }

int Hand_Analyzer_Basic::get_open_meld_num() const { return (num_and_flags & 0x00000F00) >> 8; }

void Hand_Analyzer_Basic::set_open_meld_num(const int num) {
  assert(num <= 4);
  num_and_flags = (num_and_flags & 0xFFFFF0FF) + (uint32_t(num) << 8);
}

void Hand_Analyzer_Basic::add_open_meld_num(const int num) {
  num_and_flags += (uint32_t(num) << 8);
}

void Hand_Analyzer_Basic::reduce_open_meld_num(const int num) {
  num_and_flags -= (uint32_t(num) << 8);
}

int Hand_Analyzer_Basic::get_concealed_kan_num() const {
  return (num_and_flags & 0x0000F000) >> 12;
}

void Hand_Analyzer_Basic::set_concealed_kan_num(const int num) {
  assert(num <= 4);
  num_and_flags = (num_and_flags & 0xFFFF0FFF) + (uint32_t(num) << 12);
}

void Hand_Analyzer_Basic::add_concealed_kan_num(const int num) {
  num_and_flags += (uint32_t(num) << 12);
}

int Hand_Analyzer_Basic::get_meld_shanten_num() const { return (num_and_flags & 0x000F0000) >> 16; }

int Hand_Analyzer_Basic::get_seven_pairs_shanten_num() const {
  return (num_and_flags & 0x00F00000) >> 20;
}

int Hand_Analyzer_Basic::get_win_shanten_num() const { return (num_and_flags & 0x0F000000) >> 24; }

void Hand_Analyzer_Basic::set_meld_shanten_num(const int num) {
  num_and_flags = (num_and_flags & 0xFFF0FFFF) + (uint32_t(num) << 16);
}

void Hand_Analyzer_Basic::set_seven_pairs_shanten_num(const int num) {
  num_and_flags = (num_and_flags & 0xFF0FFFFF) + (uint32_t(num) << 20);
}

void Hand_Analyzer_Basic::set_win_shanten_num(const int num) {
  num_and_flags = (num_and_flags & 0xF0FFFFFF) + (uint32_t(num) << 24);
}

int Hand_Analyzer_Basic::count_tile(const int tile) const {
  if (tile % 10 == 0) {
    assert(tile != 0);
    assert(hand_bit.count_tile(tile - 5) >= hand_state.get_red_inside(tile / 10 - 1));
    return hand_state.get_red_inside(tile / 10 - 1);
  } else if (tile % 10 == 5 && tile < 30) {
    return hand_bit.count_tile(tile) - hand_state.get_red_inside(tile / 10);
  } else {
    return hand_bit.count_tile(tile);
  }
}

int Hand_Analyzer_Basic::count_tile_kind(const int tile) const {
  assert(tile % 10 != 0);
  if (tile % 10 == 5 && tile < 30) {
    return count_tile(tile) + count_tile(tile + 5);
  } else {
    return count_tile(tile);
  }
}

void Hand_Analyzer_Basic::add_tile(const int tile) {
  add_hand_num(1);
  if (tile % 10 == 0) {
    assert(tile != 0);
    hand_bit.add_tile(tile - 5);
    hand_state.set_red_inside(tile / 10 - 1, 1);
  } else {
    hand_bit.add_tile(tile);
  }
}

void Hand_Analyzer_Basic::delete_tile(const int tile) {
  reduce_hand_num(1);
  if (tile % 10 == 0) {
    assert(tile != 0);
    hand_bit.delete_tile(tile - 5);
    hand_state.set_red_inside(tile / 10 - 1, 0);
  } else {
    hand_bit.delete_tile(tile);
  }
}

void Hand_Analyzer_Basic::add_open_kan(const int tile, const int tile2) {
  assert(tile % 10 != 0);
  assert(tile == tile_kind(tile2));
  hand_state.add_open_kan(tile);
  if (tile != tile2) {
    hand_state.set_red_inside(tile / 10, 0);
    hand_state.set_red_outside(tile / 10, 1);
  }
  add_open_meld_num(1);
}

void Hand_Analyzer_Basic::change_pon_to_upgraded_kan(const int tile, const int tile2) {
  assert(tile % 10 != 0);
  assert(tile == tile_kind(tile2));

  hand_state.delete_pon(tile);
  hand_state.add_open_kan(tile);
  if (tile != tile2) {
    hand_state.set_red_inside(tile / 10, 0);
    hand_state.set_red_outside(tile / 10, 1);
  }
}

void Hand_Analyzer_Basic::add_concealed_kan(const int tile, const int tile2) {
  assert(tile % 10 != 0);
  assert(tile == tile_kind(tile2));
  hand_state.add_concealed_kan(tile);
  if (tile != tile2) {
    hand_state.set_red_inside(tile / 10, 0);
    hand_state.set_red_outside(tile / 10, 1);
  }
  add_open_meld_num(1);
  add_concealed_kan_num(1);
}

void Hand_Analyzer_Basic::set_open_meld(const Open_Meld_Vector &open_meld) {
  set_open_meld_num(0);
  set_concealed_kan_num(0);
  for (const auto &elem : open_meld) {
    add_open_meld_num(1);
    if (elem.type == FT_CONCEALED_KAN) {
      add_concealed_kan_num(1);
    }
  }
  hand_state.set_open_meld(open_meld);
}

bool Hand_Analyzer_Basic::rule_base_decision(const int my_pid) {
  if (get_riichi() == 1) {
    return false;
  }
  if (get_shanten_num() >= 4) {
    return true;
  }
  if (get_meld_shanten_num() >= 5 && get_seven_pairs_shanten_num() >= 3) {
    return true;
  }
  return false;
}

void Hand_Analyzer_Basic::pattern_push(const int my_pid_new, const Game_State &game_state,
                                       const Tile_Array &hand_kind,
                                       Hand_Pattern_Source &pattern_source) {}

void Hand_Analyzer_Basic::pattern_seven_pairs_push(Hand_Pattern_Source &pattern_source) {}

void Hand_Analyzer::pattern_push(const int my_pid_new, const Game_State &game_state,
                                 const Tile_Array &hand_kind, Hand_Pattern_Source &pattern_source) {
  const Open_Meld_Vector open_meld_kind = tile_kind(game_state.player_state[my_pid_new].open_meld);
  const Tile_Array tile_visible_all = get_tile_visible_all(game_state);
  const Tile_Array tile_visible_kind = sum_tile_array(tile_visible_all, hand_kind);
  Hand_Pattern hand_pattern(my_pid_new, pattern_source.head_tile, pattern_source.meld_tile,
                            pattern_source.proto_sequence_tile, pattern_source.isolated_tile_1,
                            pattern_source.isolated_tile_2, hand_state, get_open_meld_num());
  hand_pattern.cal_shanten();
  if (hand_pattern.shanten_num <= get_meld_change_num_max()) {
    hand_pattern.cal_tile_in_block();
    hand_pattern.cal_tile_in_pattern(hand_kind, open_meld_kind);
    if (hand_pattern.tile_in_pattern.size() > 0) {
      for (int nip = 0; nip < hand_pattern.tile_in_pattern.size(); nip++) {
        Hand_Inout_Pattern hand_io_pattern;
        hand_io_pattern.priority = hand_pattern.cal_priority(game_state, tile_visible_kind, nip);
        push_back_all(hand_io_pattern.tile_in_pattern, hand_pattern.tile_in_pattern[nip]);
        push_back_all(hand_io_pattern.tile_out_pattern, hand_pattern.remain);
        inout_pattern_vec[hand_pattern.shanten_num].push_back(hand_io_pattern);
      }
    }
  } else {
    hand_pattern.cal_tile_in_block();
    hand_pattern.cal_tile_in_pattern(hand_kind, open_meld_kind);
    if (hand_pattern.tile_in_pattern.size() > 0) {
      for (int nip = 0; nip < hand_pattern.tile_in_pattern.size(); nip++) {
        Hand_Inout_Pattern hand_io_pattern;
        hand_io_pattern.priority = hand_pattern.cal_priority(game_state, tile_visible_kind, nip);
        push_back_all(hand_io_pattern.tile_in_pattern, hand_pattern.tile_in_pattern[nip]);
        push_back_all(hand_io_pattern.tile_out_pattern, hand_pattern.remain);
        inout_pattern_vec[hand_pattern.shanten_num].push_back(hand_io_pattern);
      }
    }
  }
}

void Hand_Analyzer::pattern_seven_pairs_push(Hand_Pattern_Source &pattern_source) {
  Hand_Pattern_Seven_Pairs hand_pattern_seven_pairs(
      pattern_source.head_tile, pattern_source.isolated_tile_1, pattern_source.isolated_tile_2);
  hand_pattern_seven_pairs.cal_shanten();
  if (hand_pattern_seven_pairs.shanten_num <= seven_pairs_change_num_max) {
    hand_pattern_seven_pairs.cal_tile_in_pattern();
    if (hand_pattern_seven_pairs.tile_in_pattern.size() > 0) {
      pattern_seven_pairs_vec[hand_pattern_seven_pairs.shanten_num].push_back(
          hand_pattern_seven_pairs);
    }
  }
}

template <class Win_Vector>
void Hand_Analyzer_Basic::tenpai_check(const int my_pid_new, const Game_State &game_state,
                                       const Tile_Array &hand_kind_counts, Tile_Array &hand_cut,
                                       Tile_Array &hand_tmp, Win_Vector &win_vector) {
  int rest = 0;
  for (int tile = 0; tile < 38; tile++) {
    rest = rest + hand_tmp[tile];
  }
  if (rest == 0) {
    // The input is already a meld-hand win shape. We do nothing here.
  } else if (rest == 1) {
    for (int tile = 0; tile < 38; tile++) {
      if (hand_tmp[tile] && using_tile_kind_num(tile) != 4) {
        set_tenpai(1);
        win_push(my_pid_new, game_state, hand_kind_counts, hand_cut, hand_tmp, tile, MT_PAIR_WAIT,
                 0, win_vector);
      }
    }
  } else if (rest == 2) {
    for (int tile = 0; tile < 38; tile++) {
      if (hand_tmp[tile] == 2 && using_tile_kind_num(tile) != 4) {
        set_tenpai(1);
        win_push(my_pid_new, game_state, hand_kind_counts, hand_cut, hand_tmp, tile, MT_DUAL_PAIR,
                 0, win_vector);
      }
    }
    for (int tile = 0; tile < 30; tile++) {
      if (hand_tmp[tile] && hand_tmp[tile + 1]) {
        if (tile % 10 == 1) {
          if (using_tile_kind_num(tile + 2) != 4) {
            set_tenpai(1);
            win_push(my_pid_new, game_state, hand_kind_counts, hand_cut, hand_tmp, tile + 2,
                     MT_EDGE_WAIT, 0, win_vector);
          }
        } else if (tile % 10 == 8) {
          if (using_tile_kind_num(tile - 1) != 4) {
            set_tenpai(1);
            win_push(my_pid_new, game_state, hand_kind_counts, hand_cut, hand_tmp, tile - 1,
                     MT_EDGE_WAIT, 0, win_vector);
          }
        } else {
          if (using_tile_kind_num(tile - 1) != 4) {
            set_tenpai(1);
            win_push(my_pid_new, game_state, hand_kind_counts, hand_cut, hand_tmp, tile - 1,
                     MT_OPEN_WAIT, 0, win_vector);
          }
          if (using_tile_kind_num(tile + 2) != 4) {
            set_tenpai(1);
            win_push(my_pid_new, game_state, hand_kind_counts, hand_cut, hand_tmp, tile + 2,
                     MT_OPEN_WAIT, 0, win_vector);
          }
        }
      }
      if (hand_tmp[tile] && hand_tmp[tile + 2]) {
        if (tile % 10 != 9) {
          if (using_tile_kind_num(tile + 1) != 4) {
            set_tenpai(1);
            win_push(my_pid_new, game_state, hand_kind_counts, hand_cut, hand_tmp, tile + 1,
                     MT_KANCHAN, 0, win_vector);
          }
        }
      }
    }
  }
  if (get_tenpai() == 1) {
    set_meld_shanten_num(0);
  }
}

void Hand_Analyzer_Basic::cut_isolated_tile(const int my_pid_new, const Game_State &game_state,
                                            const Tile_Array &hand_kind_counts,
                                            Tile_Array &hand_cut, Tile_Array &hand_tmp,
                                            int meld_num, int head_num, int candidate_num,
                                            int start, Hand_Pattern_Source &pattern_source) {
  if (meld_num + get_open_meld_num() + candidate_num < 4) {
    for (int tile = start; tile < 38; tile++) {
      if (hand_tmp[tile] >= 1) {
        hand_tmp[tile]--;
        pattern_source.isolated_tile_1.push_back(tile);
        cut_isolated_tile(my_pid_new, game_state, hand_kind_counts, hand_cut, hand_tmp, meld_num,
                          head_num, candidate_num + 1, tile, pattern_source);
        hand_tmp[tile]++;
        pattern_source.isolated_tile_1.pop_back();
      }
    }
  }

  for (int tile = 0; tile < 38; tile++) {
    for (int i = 0; i < hand_tmp[tile]; i++) {
      pattern_source.isolated_tile_2.push_back(tile);
    }
  }

  pattern_push(my_pid_new, game_state, hand_kind_counts, pattern_source);

  pattern_source.isolated_tile_2.erase(pattern_source.isolated_tile_2.begin(),
                                       pattern_source.isolated_tile_2.end());
}

void Hand_Analyzer_Basic::analyze_isolated_tile(const int my_pid_new, const Game_State &game_state,
                                                const Tile_Array &hand_kind_counts,
                                                Tile_Array &hand_cut, Tile_Array &hand_tmp,
                                                int meld_num, int head_num, int candidate_num,
                                                Hand_Pattern_Source &pattern_source) {
  if (head_num == 0) {
    for (int tile = 0; tile < 38; tile++) {
      if (hand_tmp[tile] >= 1) {
        hand_tmp[tile] = hand_tmp[tile] - 1;
        pattern_source.head_tile.push_back(tile);
        cut_isolated_tile(my_pid_new, game_state, hand_kind_counts, hand_cut, hand_tmp, meld_num,
                          head_num + 1, candidate_num, 0, pattern_source);
        hand_tmp[tile] = hand_tmp[tile] + 1;
        pattern_source.head_tile.pop_back();
      }
    }
  }
  cut_isolated_tile(my_pid_new, game_state, hand_kind_counts, hand_cut, hand_tmp, meld_num,
                    head_num, candidate_num, 0, pattern_source);
}

void Hand_Analyzer_Basic::cut_proto_sequence(const int my_pid_new, const Game_State &game_state,
                                             const Tile_Array &hand_kind_counts,
                                             Tile_Array &hand_cut, Tile_Array &hand_tmp,
                                             int meld_num, int head_num, int candidate_num,
                                             int start, Hand_Pattern_Source &pattern_source) {
  if (meld_num + get_open_meld_num() + candidate_num < 4) {
    for (int tile = start; tile < 38; tile++) {
      if (hand_tmp[tile] == 2 && hand_kind_counts[tile] != 4) {
        hand_tmp[tile] -= 2;
        pattern_source.proto_sequence_tile.push_back(tile);
        pattern_source.proto_sequence_tile.push_back(tile);
        cut_proto_sequence(my_pid_new, game_state, hand_kind_counts, hand_cut, hand_tmp, meld_num,
                           head_num, candidate_num + 1, tile, pattern_source);
        hand_tmp[tile] += 2;
        pattern_source.proto_sequence_tile.pop_back();
        pattern_source.proto_sequence_tile.pop_back();
      }
      if (tile < 30 && hand_tmp[tile] && hand_tmp[tile + 1]) {
        hand_tmp[tile]--;
        hand_tmp[tile + 1]--;
        pattern_source.proto_sequence_tile.push_back(tile);
        pattern_source.proto_sequence_tile.push_back(tile + 1);
        cut_proto_sequence(my_pid_new, game_state, hand_kind_counts, hand_cut, hand_tmp, meld_num,
                           head_num, candidate_num + 1, tile, pattern_source);
        hand_tmp[tile]++;
        hand_tmp[tile + 1]++;
        pattern_source.proto_sequence_tile.pop_back();
        pattern_source.proto_sequence_tile.pop_back();
      }
      if (tile < 30 && tile % 10 <= 8 && hand_tmp[tile] && hand_tmp[tile + 2]) {
        hand_tmp[tile]--;
        hand_tmp[tile + 2]--;
        pattern_source.proto_sequence_tile.push_back(tile);
        pattern_source.proto_sequence_tile.push_back(tile + 2);
        cut_proto_sequence(my_pid_new, game_state, hand_kind_counts, hand_cut, hand_tmp, meld_num,
                           head_num, candidate_num + 1, tile, pattern_source);
        hand_tmp[tile]++;
        hand_tmp[tile + 2]++;
        pattern_source.proto_sequence_tile.pop_back();
        pattern_source.proto_sequence_tile.pop_back();
      }
    }
  }

  int tmp = 8 - (meld_num + get_open_meld_num()) * 2 - candidate_num - head_num;
  if (meld_num + get_open_meld_num() == 4) {
    int flag = 1;
    for (int tile = 0; tile < 38; tile++) {
      if (hand_tmp[tile] == 1 && hand_kind_counts[tile] != 4) {
        flag = 0;
      }
    }
    if (flag == 1) {
      tmp++;
    }
  }
  if (tmp < get_meld_shanten_num()) {
    set_meld_shanten_num(tmp);
  }

  // In theory, discarding shapes that fail tmp <= get_meld_change_num_max() here is faster for the
  // best move. We relax this check to output data for study.
  if (get_pattern() == 1 && tmp <= get_meld_change_num_max()) {
    analyze_isolated_tile(my_pid_new, game_state, hand_kind_counts, hand_cut, hand_tmp, meld_num,
                          head_num, candidate_num, pattern_source);
  }
}

template <class Win_Vector>
void Hand_Analyzer_Basic::analyze_proto_sequence(const int my_pid_new, const Game_State &game_state,
                                                 const Tile_Array &hand_kind_counts,
                                                 Tile_Array &hand_cut, Tile_Array &hand_tmp,
                                                 Hand_Pattern_Source &pattern_source,
                                                 Win_Vector &win_vector) {
  int head_num_tmp = 0;

  // Check which tile we chose as the pair.
  for (int tile = 0; tile < 38; tile++) {
    if (hand_kind_counts[tile] == hand_cut[tile] + 2) {
      head_num_tmp = 1;
    }
  }

  for (int tile = 0; tile < 38; tile++) {
    if (hand_tmp[tile] >= 3) {
      return;
    }
  }

  if (get_pattern() == 0) {
    for (int j = 0; j < 3; j++) {
      for (int i = 1; i <= 7; i++) {
        if (hand_tmp[10 * j + i] && hand_tmp[10 * j + i + 1] && hand_tmp[10 * j + i + 2]) {
          return;
        }
      }
    }
  }
  // Remove clearly useless patterns up to here.

  tenpai_check(my_pid_new, game_state, hand_kind_counts, hand_cut, hand_tmp, win_vector);

  if (get_tenpai() == 0 || get_pattern() == 1) {
    int rest = 0;
    for (int tile = 0; tile < 38; tile++) {
      rest = rest + hand_tmp[tile];
    }
    int meld_num_tmp = (std::accumulate(hand_kind_counts.begin(), hand_kind_counts.end(), 0) -
                        rest - head_num_tmp * 2) /
                       3;
    cut_proto_sequence(my_pid_new, game_state, hand_kind_counts, hand_cut, hand_tmp, meld_num_tmp,
                       head_num_tmp, 0, 0, pattern_source);
  }
}

template <class Win_Vector>
void Hand_Analyzer_Basic::cut_sequence(const int my_pid_new, const Game_State &game_state,
                                       const Tile_Array &hand_kind_counts, Tile_Array &hand_cut,
                                       Tile_Array &hand_tmp, int start,
                                       Hand_Pattern_Source &pattern_source,
                                       Win_Vector &win_vector) {
  for (int tile = start; tile < 30; tile++) {
    if (hand_tmp[tile] && hand_tmp[tile + 1] && hand_tmp[tile + 2]) {
      hand_tmp[tile] = hand_tmp[tile] - 1;
      hand_tmp[tile + 1] = hand_tmp[tile + 1] - 1;
      hand_tmp[tile + 2] = hand_tmp[tile + 2] - 1;
      pattern_source.meld_tile.push_back(tile);
      pattern_source.meld_tile.push_back(tile + 1);
      pattern_source.meld_tile.push_back(tile + 2);
      cut_sequence(my_pid_new, game_state, hand_kind_counts, hand_cut, hand_tmp, tile,
                   pattern_source, win_vector);
      hand_tmp[tile] = hand_tmp[tile] + 1;
      hand_tmp[tile + 1] = hand_tmp[tile + 1] + 1;
      hand_tmp[tile + 2] = hand_tmp[tile + 2] + 1;
      pattern_source.meld_tile.pop_back();
      pattern_source.meld_tile.pop_back();
      pattern_source.meld_tile.pop_back();
    }
  }

  analyze_proto_sequence(my_pid_new, game_state, hand_kind_counts, hand_cut, hand_tmp,
                         pattern_source, win_vector);
}

template <class Win_Vector>
void Hand_Analyzer_Basic::cut_triplet(const int my_pid_new, const Game_State &game_state,
                                      const Tile_Array &hand_kind_counts, Tile_Array &hand_tmp,
                                      int start, Hand_Pattern_Source &pattern_source,
                                      Win_Vector &win_vector) {
  for (int tile = start; tile < 38; tile++) {
    if (hand_tmp[tile] >= 3) {
      hand_tmp[tile] = hand_tmp[tile] - 3;
      pattern_source.meld_tile.push_back(tile);
      pattern_source.meld_tile.push_back(tile);
      pattern_source.meld_tile.push_back(tile);
      cut_triplet(my_pid_new, game_state, hand_kind_counts, hand_tmp, tile, pattern_source,
                  win_vector);
      hand_tmp[tile] = hand_tmp[tile] + 3;
      pattern_source.meld_tile.pop_back();
      pattern_source.meld_tile.pop_back();
      pattern_source.meld_tile.pop_back();
    }
  }

  Tile_Array hand_cut = hand_tmp;
  cut_sequence(my_pid_new, game_state, hand_kind_counts, hand_cut, hand_tmp, 0, pattern_source,
               win_vector);
}

void Hand_Analyzer_Basic::seven_pairs_cut_isolated_tile(const Tile_Array &hand_kind_counts,
                                                        Tile_Array &hand_tmp, int start,
                                                        Hand_Pattern_Source &pattern_source) {
  for (int tile = start; tile < 38; tile++) {
    if (hand_tmp[tile] == 1 && hand_kind_counts[tile] == 1 &&
        pattern_source.head_tile.size() / 2 + pattern_source.isolated_tile_1.size() < 7) {
      hand_tmp[tile] = hand_tmp[tile] - 1;
      pattern_source.isolated_tile_1.push_back(tile);
      seven_pairs_cut_isolated_tile(hand_kind_counts, hand_tmp, tile + 1, pattern_source);
      hand_tmp[tile] = hand_tmp[tile] + 1;
      pattern_source.isolated_tile_1.pop_back();
    }
  }
  for (int tile = 0; tile < 38; tile++) {
    for (int i = 0; i < hand_tmp[tile]; i++) {
      pattern_source.isolated_tile_2.push_back(tile);
    }
  }

  pattern_seven_pairs_push(pattern_source);

  pattern_source.isolated_tile_2.erase(pattern_source.isolated_tile_2.begin(),
                                       pattern_source.isolated_tile_2.end());
}

void Hand_Analyzer_Basic::seven_pairs_cut_head(const Tile_Array &hand_kind_counts,
                                               Tile_Array &hand_tmp, int start,
                                               Hand_Pattern_Source &pattern_source) {
  for (int tile = start; tile < 38; tile++) {
    if (hand_tmp[tile] >= 2) {
      hand_tmp[tile] = hand_tmp[tile] - 2;
      pattern_source.head_tile.push_back(tile);
      pattern_source.head_tile.push_back(tile);
      seven_pairs_cut_head(hand_kind_counts, hand_tmp, tile + 1, pattern_source);
      hand_tmp[tile] = hand_tmp[tile] + 2;
      pattern_source.head_tile.pop_back();
      pattern_source.head_tile.pop_back();
    }
  }
  seven_pairs_cut_isolated_tile(hand_kind_counts, hand_tmp, 0, pattern_source);
}

template <class Win_Vector>
void Hand_Analyzer_Basic::seven_pairs_shanten(const int my_pid_new, const Game_State &game_state,
                                              const Tile_Array &hand_kind_counts,
                                              Tile_Array &hand_tmp,
                                              Hand_Pattern_Source &pattern_source,
                                              Win_Vector &win_vector) {
  int head_num = 0;
  int isolated_tile_num = 0;
  for (int tile = 0; tile < 38; tile++) {
    if (hand_kind_counts[tile] >= 2) {
      head_num++;
    } else if (hand_kind_counts[tile] > 0) {
      isolated_tile_num++;
    }
  }
  if (head_num == 7) {
    // The input is already a seven pairs win. We do nothing here.
  } else if (head_num == 6) {
    for (int tile = 0; tile < 38; tile++) {
      if (hand_kind_counts[tile] == 1) {
        set_tenpai(1);
        win_push(my_pid_new, game_state, hand_kind_counts, hand_kind_counts, hand_kind_counts, tile,
                 MT_PAIR_WAIT, 1, win_vector);
      }
    }
  }

  int tmp = 13 - 2 * head_num - std::min(7 - head_num, isolated_tile_num);
  if (tmp < get_seven_pairs_shanten_num()) {
    set_seven_pairs_shanten_num(std::max(0, tmp));
  }

  if (get_pattern() == 1 && get_seven_pairs_shanten_num() <= get_seven_pairs_change_num_max() &&
      get_seven_pairs_shanten_num() <= 3) {
    seven_pairs_cut_head(hand_kind_counts, hand_tmp, 0, pattern_source);
  }
}

template <class Win_Vector>
void Hand_Analyzer_Basic::analyze_hand(const int my_pid_new, const Game_State &game_state,
                                       Win_Vector &win_vector) {
  Tile_Array hand_kind_counts;
  Hand_Pattern_Source pattern_source;

  hand_bit.insert_to_array38(hand_kind_counts);
  set_hand_num(std::accumulate(hand_kind_counts.begin(), hand_kind_counts.end(), 0));
  assert(get_hand_num() == hand_bit.get_size());

  reset_tenpai();
  Tile_Array hand_tmp = hand_kind_counts;
  for (int tile = 0; tile < 38; tile++) {
    if (hand_kind_counts[tile] >= 2) {
      hand_tmp[tile] = hand_tmp[tile] - 2;
      pattern_source.head_tile.push_back(tile);
      pattern_source.head_tile.push_back(tile);
      cut_triplet(my_pid_new, game_state, hand_kind_counts, hand_tmp, 0, pattern_source,
                  win_vector);
      hand_tmp[tile] = hand_tmp[tile] + 2;
      pattern_source.head_tile.pop_back();
      pattern_source.head_tile.pop_back();
    }
  }
  cut_triplet(my_pid_new, game_state, hand_kind_counts, hand_tmp, 0, pattern_source, win_vector);

  if (get_open_meld_num() == 0) {
    seven_pairs_shanten(my_pid_new, game_state, hand_kind_counts, hand_tmp, pattern_source,
                        win_vector);
  }
}

template void Hand_Analyzer_Basic::analyze_hand<boost::container::static_vector<Win_Info, 10>>(
    const int my_pid_new, const Game_State &game_state,
    boost::container::static_vector<Win_Info, 10> &win_vector);
template void Hand_Analyzer_Basic::analyze_hand<
    boost::container::static_vector<Win_Calc, MAX_WIN_NUM_PER_THREAD>>(
    const int my_pid_new, const Game_State &game_state,
    boost::container::static_vector<Win_Calc, MAX_WIN_NUM_PER_THREAD> &win_vector);
template void Hand_Analyzer_Basic::analyze_hand<bool>(const int my_pid_new,
                                                      const Game_State &game_state,
                                                      bool &win_vector);

void Hand_Analyzer_Basic::analyze_tenpai(const int my_pid_new, const Game_State &game_state) {
  bool dummy = false;
  analyze_hand(my_pid_new, game_state, dummy);
}

bool Hand_Analyzer_Basic::can_concealed_kan_after_riichi(const int tsumo_tile) const {
  assert(get_riichi() == 1);
  Tile_Array hand_kind_counts;
  hand_bit.insert_to_array38(hand_kind_counts);
  Hand_State2 empty_hand_state;
  return is_legal_concealed_kan_after_riichi(
      hand_kind_counts, hand_state.get_open_meld(empty_hand_state), tsumo_tile);
}

void Hand_Analyzer_Basic::print_hand() const {
  for (int tile = 1; tile < 38; tile++) {
    for (int i = 0; i < count_tile(tile); i++) {
      std::cout << tile << " ";
    }
  }
  std::cout << std::endl;
}

void Hand_Analyzer_Basic::out_tenpai() {
  if (!console_out) return;
  printf("%d shanten\n", get_shanten_num());
}

bool is_same_hand_proto_sequence(const Tile_Array &hand, const Hand_Analyzer_Basic &hand_analyzer) {
  for (int tile = 1; tile < 38; tile++) {
    if (hand[tile] != hand_analyzer.count_tile(tile)) {
      return false;
    }
  }
  return true;
}

int find_tile_out_proto_sequence(const Tile_Array &hand, const Hand_Analyzer_Basic &hand_analyzer) {
  int discarded = 0;
  for (int tile = 1; tile < 38; tile++) {
    const int difference = hand[tile] - hand_analyzer.count_tile(tile);
    if (difference == 0) continue;
    if (difference != 1 || discarded != 0) return 0;
    discarded = tile;
  }
  return discarded;
}

int isolated_tile_needless_num(const Tile_Array &hand, const Game_State &game_state, const int pid,
                               const Tile_Array tile_visible_all) {
  int res = 0;
  std::vector<int> cand;
  for (int hc = 0; hc < 3; hc++) {
    if (hand[hc * 10 + 1] == 1 && hand[hc * 10 + 2] == 0 && hand[hc * 10 + 3] == 0) {
      cand.push_back(hc * 10 + 1);
    }
    if (hand[hc * 10 + 9] == 1 && hand[hc * 10 + 8] == 0 && hand[hc * 10 + 7] == 0) {
      cand.push_back(hc * 10 + 9);
    }
  }
  for (int tile = 31; tile < 38; tile++) {
    if (hand[tile] == 1) {
      if (tile == 31 + game_state.player_state[pid].self_wind && tile_visible_all[tile] < 2) {
        continue;
      } else if (35 <= tile && tile_visible_all[tile] < 2) {
        continue;
      } else {
        cand.push_back(tile);
      }
    }
  }
  for (int cn = 0; cn < cand.size(); cn++) {
    int has_dora = 0;
    for (int dn = 0; dn < game_state.dora_marker.size(); dn++) {
      if (cand[cn] == dora_marker_to_dora(game_state.dora_marker[dn]) &&
          tile_visible_all[cand[cn]] < 3) {
        has_dora = 1;
      }
    }
    if (has_dora == 0) {
      res++;
    }
  }
  return res;
}

int isolated_tile_needless_num_proto_sequence(const Hand_Analyzer_Basic &hand_analyzer,
                                              const Game_State &game_state, const int pid,
                                              const Tile_Array tile_visible_all) {
  Tile_Array hand = {};
  for (int tile = 1; tile < 38; tile++) {
    hand[tile] = hand_analyzer.count_tile(tile);
  }
  return isolated_tile_needless_num(hand, game_state, pid, tile_visible_all);
}

void get_hand_proto_sequence(const Hand_Analyzer_Basic &hand_analyzer, int hand[38]) {
  hand[0] = 0;
  for (int tile = 1; tile < 38; tile++) {
    hand[tile] = hand_analyzer.count_tile(tile);
  }
}

void get_hand_kind_counts_proto_sequence(const Hand_Analyzer_Basic &hand_analyzer,
                                         int hand_kind_counts[38]) {
  get_hand_proto_sequence(hand_analyzer, hand_kind_counts);
  for (int i = 1; i <= 3; i++) {
    if (hand_kind_counts[i * 10] == 1) {
      hand_kind_counts[i * 10 - 5]++;
      hand_kind_counts[i * 10] = 0;
    }
  }
}

int Hand_Analyzer_Basic::using_tile_kind_num(int tile) const {
  assert(tile % 10 != 0);
  const int res = count_tile_kind(tile) + hand_state.get_tile_kind_num(tile);
  return res;
}

int Hand_Analyzer_Basic::get_shanten_num() const {
  return std::min(get_meld_shanten_num(), get_seven_pairs_shanten_num());
}

template <class Win_Vector>
void Hand_Analyzer_Basic::win_push_func_child(const Win_Info win, const int pid,
                                              const Game_State &game_state,
                                              Win_Vector &win_vector) {
  assert(win_vector.size() < 10);
  win_vector.push_back(win);

  // Organize the win records. This part rarely runs because a 10-kind wait is rare. It can be
  // unreliable.
  if (win_vector.size() == 10) {
    for (int i = 9; 0 <= i; i--) {
      for (int j = 0; j < i; j++) {
        if (win_vector[i].tile == win_vector[j].tile) {
          if (tsumo_win(win_vector[i].han_tsumo, win_vector[i].fu_tsumo,
                        game_state.player_state[pid].self_wind == 0) >
              tsumo_win(win_vector[j].han_tsumo, win_vector[j].fu_tsumo,
                        game_state.player_state[pid].self_wind == 0)) {
            win_vector[j].han_tsumo = win_vector[i].han_tsumo;
            win_vector[j].fu_tsumo = win_vector[i].fu_tsumo;
          }
          if (ron_win(win_vector[i].han_ron, win_vector[i].fu_ron,
                      game_state.player_state[pid].self_wind == 0) >
              ron_win(win_vector[j].han_ron, win_vector[j].fu_ron,
                      game_state.player_state[pid].self_wind == 0)) {
            win_vector[j].han_ron = win_vector[i].han_ron;
            win_vector[j].fu_ron = win_vector[i].fu_ron;
          }
          win_vector.erase(win_vector.begin() + i);
          break;
        }
      }
    }
  }
  assert(win_vector.size() < 10);  // A normal tenpai shape has at most nine wait kinds.
}

template <class Win_Vector>
void Hand_Analyzer_Basic::win_push(const int pid, const Game_State &game_state,
                                   const Tile_Array &hand_kind_counts, const Tile_Array &ttc,
                                   const Tile_Array &tt, const int mh, const Wait_Type mt,
                                   const bool ttf, Win_Vector &win_vector) {
  const Hand_State2 empty_hand_state;
  Open_Meld_Vector open_meld = hand_state.get_open_meld(empty_hand_state);
  Open_Meld_Vector open_meld_kind = tile_kind(open_meld);
  Win_Info win_tmp =
      calc_win(31 + game_state.round_wind, 31 + game_state.player_state[pid].self_wind,
               hand_kind_counts, ttc, tt, open_meld_kind, mh, mt, ttf);
  if (hand_state.get_riichi() == 1) {
    win_tmp.han_tsumo++;
    win_tmp.han_ron++;
  }
  int dora_num = count_dora(hand_kind_counts, open_meld,
                            game_state.dora_marker);  // This function also counts the red dora.
  for (int dn = 0; dn < game_state.dora_marker.size(); dn++) {
    if (dora_marker_to_dora(game_state.dora_marker[dn]) == win_tmp.tile) {
      dora_num++;
    }
  }
  for (int i = 0; i < 3; i++) {
    dora_num += hand_state.get_red_inside(i);
  }
  if (win_tmp.han_tsumo > 0) {
    win_tmp.han_tsumo += dora_num;
  }
  if (win_tmp.han_ron > 0) {
    win_tmp.han_ron += dora_num;
  }

  win_push_func_child(win_tmp, pid, game_state, win_vector);
}

void Hand_Analyzer_Basic::win_push(const int pid, const Game_State &game_state,
                                   const Tile_Array &hand_kind_counts, const Tile_Array &ttc,
                                   const Tile_Array &tt, const int mh, const Wait_Type mt,
                                   const bool ttf, bool flag) {
  assert(flag == false);
  // Call this function when the win vector is not set. It does nothing.
}

void Hand_Analyzer_Basic::win_push(
    const int pid, const Game_State &game_state, const Tile_Array &hand_kind_counts,
    const Tile_Array &ttc, const Tile_Array &tt, const int mh, const Wait_Type mt, const bool ttf,
    boost::container::static_vector<Win_Calc, MAX_WIN_NUM_PER_THREAD> &win_vector) {
  if (win_vector.size() < MAX_WIN_NUM_PER_THREAD) {
    const Hand_State2 empty_hand_state;
    Open_Meld_Vector open_meld = hand_state.get_open_meld(empty_hand_state);
    Open_Meld_Vector open_meld_kind = tile_kind(open_meld);
    Win_Info win_tmp =
        calc_win(31 + game_state.round_wind, 31 + game_state.player_state[pid].self_wind,
                 hand_kind_counts, ttc, tt, open_meld_kind, mh, mt, ttf);
    if (hand_state.get_riichi() == 1) {
      win_tmp.han_tsumo++;
      win_tmp.han_ron++;
    }
    int dora_num = count_dora(hand_kind_counts, open_meld,
                              game_state.dora_marker);  // This function also counts the red dora.
    for (int dn = 0; dn < game_state.dora_marker.size(); dn++) {
      if (dora_marker_to_dora(game_state.dora_marker[dn]) == win_tmp.tile) {
        dora_num++;
      }
    }
    for (int i = 0; i < 3; i++) {
      dora_num += hand_state.get_red_inside(i);
    }
    if (win_tmp.han_tsumo > 0) {
      win_tmp.han_tsumo += dora_num;
    }
    if (win_tmp.han_ron > 0) {
      win_tmp.han_ron += dora_num;
    }
    win_vector.push_back(win_info_to_win_calc(win_tmp));
  } else {
    // TODO: Report the win-vector capacity limit.
  }
}
