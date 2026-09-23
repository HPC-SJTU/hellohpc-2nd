#pragma once

#include "../share/include.hpp"
#include "bit_tile_count.hpp"
#include "hand_inout_pattern.hpp"
#include "hand_pattern.hpp"
#include "hand_state2.hpp"
#include "win.hpp"

void copy_hand(const int hand_src[38], int hand_dst[38]);
void get_hand_kind_counts(const int hand_src[38], int hand_kind_counts[38]);

class Hand_Pattern_Source {
 public:
  Hand_Pattern_Source();
  void reset();
  std::vector<int> meld_tile;
  std::vector<int> head_tile;
  std::vector<int> proto_sequence_tile;
  std::vector<int> isolated_tile_1;
  std::vector<int> isolated_tile_2;
};

class Hand_Analyzer_Basic {
  // The items in private are planned to be removed eventually.

 public:
  Hand_Analyzer_Basic();
  void reset_tenpai();
  void reset_hand_analyzer_basic();
  void reset_hand_analyzer_basic_with(const Tile_Array &hand_src, const bool is_riichi,
                                      const Open_Meld_Vector &open_meld);
  void reset_hand_analyzer_basic_with2(const Bit_Tile_Num &hand_bit_src, const Hand_State2 &ts);
  bool operator==(const Hand_Analyzer_Basic &rhs) const;
  bool operator!=(const Hand_Analyzer_Basic &rhs) const;

  void set_open_meld(const Open_Meld_Vector &open_meld);

  Bit_Tile_Num hand_bit;

  Hand_State2 hand_state;

  uint32_t num_and_flags;  // hand_state judges whether the externally visible information is the
                           // same. This stores the other information.
  int get_tenpai() const;
  int get_furiten() const;
  int get_negative() const;
  int get_kan_changed() const;

  void set_tenpai(const int flag);
  void set_furiten(const int flag);
  void set_negative(const int flag);
  void set_kan_changed(const int flag);

  int get_hand_num() const;
  void set_hand_num(const int num);
  void add_hand_num(const int num);
  void reduce_hand_num(const int num);

  int get_open_meld_num() const;
  void set_open_meld_num(const int num);
  void add_open_meld_num(const int num);
  void reduce_open_meld_num(const int num);

  int get_concealed_kan_num() const;
  void set_concealed_kan_num(const int num);
  void add_concealed_kan_num(const int num);

  int get_meld_shanten_num() const;
  int get_seven_pairs_shanten_num() const;
  int get_win_shanten_num() const;

  void set_meld_shanten_num(const int num);
  void set_seven_pairs_shanten_num(const int num);
  void set_win_shanten_num(const int num);

  virtual int get_pattern() { return 0; }
  virtual int get_seven_pairs_change_num_max() { return 0; }
  virtual int get_meld_change_num_max() { return 0; }

  void set_riichi(int flag);
  int get_riichi() const;

  int count_tile(const int tile) const;
  int count_tile_kind(const int tile) const;
  void add_tile(const int tile);
  void delete_tile(const int tile);

  void add_open_kan(const int tile_kind, const int tile2);
  void change_pon_to_upgraded_kan(const int tile_kind, const int tile2);
  void add_concealed_kan(const int tile_kind, const int tile2);

  int using_tile_kind_num(int tile) const;

  int get_shanten_num() const;

  template <class Win_Vector>
  void win_push_func_child(const Win_Info win, const int pid, const Game_State &game_state,
                           Win_Vector &win_vector);
  template <class Win_Vector>
  void win_push(const int pid, const Game_State &game_state, const Tile_Array &hand_kind_counts,
                const Tile_Array &ttc, const Tile_Array &tt, const int mh, const Wait_Type mt,
                const bool ttf, Win_Vector &win_vector);
  void win_push(const int pid, const Game_State &game_state, const Tile_Array &hand_kind_counts,
                const Tile_Array &ttc, const Tile_Array &tt, const int mh, const Wait_Type mt,
                const bool ttf, bool flag);
  void win_push(const int pid, const Game_State &game_state, const Tile_Array &hand_kind_counts,
                const Tile_Array &ttc, const Tile_Array &tt, const int mh, const Wait_Type mt,
                const bool ttf,
                boost::container::static_vector<Win_Calc, MAX_WIN_NUM_PER_THREAD> &win_vector);

  bool rule_base_decision(const int my_pid);

  virtual void pattern_push(const int my_pid_new, const Game_State &game_state,
                            const Tile_Array &hand_kind, Hand_Pattern_Source &pattern_source);
  virtual void pattern_seven_pairs_push(Hand_Pattern_Source &pattern_source);

  template <class Win_Vector>
  void tenpai_check(const int my_pid_new, const Game_State &game_state,
                    const Tile_Array &hand_kind_counts, Tile_Array &hand_cut, Tile_Array &hand_tmp,
                    Win_Vector &win_vector);
  void cut_proto_sequence(const int my_pid_new, const Game_State &game_state,
                          const Tile_Array &hand_kind_counts, Tile_Array &hand_cut,
                          Tile_Array &hand_tmp, int meld_num, int head_num, int candidate_num,
                          int start, Hand_Pattern_Source &pattern_source);
  template <class Win_Vector>
  void analyze_proto_sequence(const int my_pid_new, const Game_State &game_state,
                              const Tile_Array &hand_kind_counts, Tile_Array &hand_cut,
                              Tile_Array &hand_tmp, Hand_Pattern_Source &pattern_source,
                              Win_Vector &win_vector);
  template <class Win_Vector>
  void cut_sequence(const int my_pid_new, const Game_State &game_state,
                    const Tile_Array &hand_kind_counts, Tile_Array &hand_cut, Tile_Array &hand_tmp,
                    int start, Hand_Pattern_Source &pattern_source, Win_Vector &win_vector);
  template <class Win_Vector>
  void cut_triplet(const int my_pid_new, const Game_State &game_state,
                   const Tile_Array &hand_kind_counts, Tile_Array &hand_tmp, int start,
                   Hand_Pattern_Source &pattern_source, Win_Vector &win_vector);
  template <class Win_Vector>
  void analyze_hand(const int my_pid_new, const Game_State &game_state, Win_Vector &win_vector);

  void cut_isolated_tile(const int my_pid_new, const Game_State &game_state,
                         const Tile_Array &hand_kind_counts, Tile_Array &hand_cut,
                         Tile_Array &hand_tmp, int meld_num, int head_num, int candidate_num,
                         int start, Hand_Pattern_Source &pattern_source);
  void analyze_isolated_tile(const int my_pid_new, const Game_State &game_state,
                             const Tile_Array &hand_kind_counts, Tile_Array &hand_cut,
                             Tile_Array &hand_tmp, int meld_num, int head_num, int candidate_num,
                             Hand_Pattern_Source &pattern_source);

  void seven_pairs_cut_isolated_tile(const Tile_Array &hand_kind_counts, Tile_Array &hand_tmp,
                                     int start, Hand_Pattern_Source &pattern_source);
  void seven_pairs_cut_head(const Tile_Array &hand_kind_counts, Tile_Array &hand_tmp, int start,
                            Hand_Pattern_Source &pattern_source);
  template <class Win_Vector>
  void seven_pairs_shanten(const int my_pid_new, const Game_State &game_state,
                           const Tile_Array &hand_kind_counts, Tile_Array &hand_tmp,
                           Hand_Pattern_Source &pattern_source, Win_Vector &win_vector);

  void analyze_tenpai(const int my_pid_new, const Game_State &game_state);
  bool can_concealed_kan_after_riichi(const int tsumo_tile) const;

  void print_hand() const;
  void out_tenpai();
};

class Hand_Analyzer : public Hand_Analyzer_Basic {
 public:
  Hand_Analyzer();

  void reset_hand_analyzer();
  void reset_hand_analyzer_with(const Tile_Array &hand_src, const bool is_riichi,
                                const Open_Meld_Vector &open_meld);

  int pattern;
  int meld_change_num_max, seven_pairs_change_num_max;
  std::vector<Hand_Inout_Pattern> inout_pattern_vec[9];
  std::vector<Hand_Pattern_Seven_Pairs> pattern_seven_pairs_vec[7];

  int get_pattern() { return pattern; }
  int get_seven_pairs_change_num_max() { return seven_pairs_change_num_max; }
  int get_meld_change_num_max() { return meld_change_num_max; }

  void pattern_push(const int my_pid_new, const Game_State &game_state, const Tile_Array &hand_kind,
                    Hand_Pattern_Source &pattern_source);
  void pattern_seven_pairs_push(Hand_Pattern_Source &pattern_source);
};

bool is_same_hand_proto_sequence(const Tile_Array &hand, const Hand_Analyzer_Basic &hand_analyzer);

int find_tile_out_proto_sequence(const Tile_Array &hand, const Hand_Analyzer_Basic &hand_analyzer);

int isolated_tile_needless_num(const Tile_Array &hand, const Game_State &game_state, const int pid,
                               const Tile_Array tile_visible_all);
int isolated_tile_needless_num_proto_sequence(const Hand_Analyzer_Basic &hand_analyzer,
                                              const Game_State &game_state, const int pid,
                                              const Tile_Array tile_visible_all);

void get_hand_proto_sequence(const Hand_Analyzer_Basic &hand_analyzer, int hand[38]);

void get_hand_kind_counts_proto_sequence(const Hand_Analyzer_Basic &hand_analyzer,
                                         int hand_kind_counts[38]);
