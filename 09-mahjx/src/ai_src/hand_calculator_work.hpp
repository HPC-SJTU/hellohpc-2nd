#pragma once

#include "../share/include.hpp"
#include "hand_action.hpp"
#include "hand_group.hpp"

class Hand_Calculator_Work {
 public:
  Hand_Calculator_Work();

  boost::container::static_vector<Hand_Group, MAX_CANDIDATES_NUM> candidates_work;
  std::array<
      boost::container::static_vector<Hand_Analyzer_Basic, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
      CAL_NUM_THREAD>
      cal_proto_sequence_value_work;

  std::array<boost::container::static_vector<Tsumo_Edge, MAX_TSUMO_EDGE_NUM_PER_THREAD>,
             CAL_NUM_THREAD>
      tsumo_edge_work;
  std::array<boost::container::static_vector<Hand_Action, MAX_EDGE_NUM_PER_THREAD>, CAL_NUM_THREAD>
      cand_graph_sub_tsumo_work;
  std::array<boost::container::static_vector<Hand_Action, MAX_EDGE_NUM_PER_THREAD>, CAL_NUM_THREAD>
      cand_graph_sub_open_meld_work;
  std::array<boost::container::static_vector<Win_Calc, MAX_WIN_NUM_PER_THREAD>, CAL_NUM_THREAD>
      win_graph_work;

  std::array<std::array<std::array<int, 3>, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>, CAL_NUM_THREAD>
      cand_graph_sub_tsumo_loc;
  std::array<std::array<std::array<int, 3>, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>, CAL_NUM_THREAD>
      cand_graph_sub_open_meld_loc;
  std::array<std::array<std::array<int, 3>, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>, CAL_NUM_THREAD>
      win_graph_loc;

  std::array<std::array<std::array<double, 2>, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>, CAL_NUM_THREAD>
      win_prob_work;
  std::array<std::array<std::array<double, 2>, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>, CAL_NUM_THREAD>
      win_exp_work;
  std::array<std::array<std::array<double, 2>, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>, CAL_NUM_THREAD>
      win_prob_to_work;
  std::array<std::array<std::array<double, 2>, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>, CAL_NUM_THREAD>
      win_exp_to_work;

  std::array<std::array<std::array<std::array<double, 5>, 2>, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
             CAL_NUM_THREAD>
      win_han_prob_work;
  std::array<std::array<std::array<std::array<double, 5>, 2>, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>,
             CAL_NUM_THREAD>
      win_han_prob_to_work;

  std::array<std::array<std::array<double, 2>, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>, CAL_NUM_THREAD>
      tenpai_prob_work;
  std::array<std::array<std::array<double, 2>, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>, CAL_NUM_THREAD>
      tenpai_prob_to_work;
  std::array<std::array<std::array<double, 2>, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>, CAL_NUM_THREAD>
      points_exp_work;
  std::array<std::array<std::array<double, 2>, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>, CAL_NUM_THREAD>
      points_exp_to_work;

  std::array<std::array<double, MAX_PROTO_SEQUENCE_NUM_PER_THREAD>, CAL_NUM_THREAD> fold_exp_work;

  const std::array<int, 3> &get_const_tsumo_edge_loc(const int cn, const int gn) const;
  const std::array<int, 3> &get_const_open_meld_edge_loc(const int cn, const int gn) const;
  const std::array<int, 3> &get_const_win_loc(const int cn, const int gn) const;
};