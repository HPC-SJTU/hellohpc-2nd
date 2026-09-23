#pragma once

#include "../share/include.hpp"
#include "hand_analyzer.hpp"
#include "hand_change.hpp"
#include "hand_state2.hpp"
#include "win.hpp"

const int CAL_NUM_THREAD = NPROCS;

const int MAX_CANDIDATES_NUM = 200000;
const int MAX_PROTO_SEQUENCE_NUM_PER_THREAD = 100000;
const int BUF_FOR_RIICHI_PER_THREAD = 10000;
const int MAX_EDGE_NUM_PER_THREAD = 100000 * 64;
const int MAX_TSUMO_EDGE_NUM_PER_THREAD = 100000 * 16;
const int MAX_PROTO_SEQUENCE_NUM_PER_GROUP = 128;
const int BUF_FOR_RIICHI_PER_GROUP = 10;

const int MAX_TSUMO_NUM = 20;
// The tsumo count is not 2. However, 2 is enough memory-wise, so this is how it is. Fix it when it
// is not enough.

class Hand_Location {
 public:
  Hand_Location();
  Hand_Location(const int first, const int second);
  uint32_t data;

  int get_first() const;
  int get_second() const;
};

int loc_intvec(const std::vector<int> &vec, const int num);
Open_Meld_Type open_meld_action_type_to_open_meld_type(const int open_meld_action_type);

class Hand_Group {
 public:
  Hand_Group();
  void reset();

  Hand_Change hand_change;
  boost::container::static_vector<Hand_Location, MAX_PROTO_SEQUENCE_NUM_PER_GROUP>
      proto_sequence_loc;

  void set_proto_sequence_value_init(
      const Bit_Tile_Num &hand_bit_original, const Hand_State2 &ts, const int open_meld_cand_tile,
      boost::unordered_map<Hand_State2, int> &ts_map,
      std::array<
          boost::container::static_vector<Hand_Analyzer_Basic, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
          CAL_NUM_THREAD> &cal_proto_sequence_value,
      const int thread_num);

  void add_open_meld_child(
      Hand_Analyzer_Basic &hand_analyzer, int open_meld_cand[38], const int open_meld_cand_copy[38],
      int open_meld_num_begin, int open_meld_num_end, int open_meld_must, int kan_cand[38],
      boost::unordered_map<Hand_State2, int> &ts_map, const int open_meld_type, int tile0,
      int tile1, int tile2, int tile3,
      std::array<
          boost::container::static_vector<Hand_Analyzer_Basic, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
          CAL_NUM_THREAD> &cal_proto_sequence_value,
      const int thread_num);
  void add_open_meld(
      Hand_Analyzer_Basic hand_analyzer, int open_meld_cand[38], const int open_meld_cand_copy[38],
      int open_meld_num_begin, int open_meld_num_end, int open_meld_must, int kan_cand[38],
      boost::unordered_map<Hand_State2, int> &ts_map,
      std::array<
          boost::container::static_vector<Hand_Analyzer_Basic, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
          CAL_NUM_THREAD> &cal_proto_sequence_value,
      const int thread_num);

  void add_concealed_kan(
      const int kan_cand[38], boost::unordered_map<Hand_State2, int> &ts_map,
      std::array<
          boost::container::static_vector<Hand_Analyzer_Basic, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
          CAL_NUM_THREAD> &cal_proto_sequence_value,
      const int thread_num);

  void add_open_kan(const int kan_cand[38], boost::unordered_map<Hand_State2, int> &ts_map,
                    std::array<boost::container::static_vector<Hand_Analyzer_Basic,
                                                               MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
                               CAL_NUM_THREAD> &cal_proto_sequence_value,
                    const int thread_num);

  void add_upgraded_kan(
      const int kan_cand[38], boost::unordered_map<Hand_State2, int> &ts_map,
      std::array<
          boost::container::static_vector<Hand_Analyzer_Basic, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
          CAL_NUM_THREAD> &cal_proto_sequence_value,
      const int thread_num);

  void analyze_all_tenpai(
      const int my_pid, const Game_State &game_state,
      std::array<
          boost::container::static_vector<Hand_Analyzer_Basic, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
          CAL_NUM_THREAD> &cal_proto_sequence_value);

  void add_riichi(boost::unordered_map<Hand_State2, int> &ts_map,
                  std::array<boost::container::static_vector<Hand_Analyzer_Basic,
                                                             MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
                             CAL_NUM_THREAD> &cal_proto_sequence_value,
                  const int thread_num);

  void analyze_all_win(
      const int my_pid, const Game_State &game_state,
      std::array<
          boost::container::static_vector<Hand_Analyzer_Basic, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
          CAL_NUM_THREAD> &cal_proto_sequence_value,
      boost::container::static_vector<Win_Calc, MAX_WIN_NUM_PER_THREAD> &win_graph,
      std::array<std::array<std::array<int, 3>, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>, CAL_NUM_THREAD>
          &win_graph_loc,
      const int thread_num);
};
