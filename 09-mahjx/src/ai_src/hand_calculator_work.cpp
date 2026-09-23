#include "hand_calculator_work.hpp"

Hand_Calculator_Work::Hand_Calculator_Work() { candidates_work.clear(); }

const std::array<int, 3> &Hand_Calculator_Work::get_const_tsumo_edge_loc(const int cn,
                                                                         const int gn) const {
  const Hand_Location &proto_sequence_loc = candidates_work[cn].proto_sequence_loc[gn];
  return cand_graph_sub_tsumo_loc[proto_sequence_loc.get_first()][proto_sequence_loc.get_second()];
}

const std::array<int, 3> &Hand_Calculator_Work::get_const_open_meld_edge_loc(const int cn,
                                                                             const int gn) const {
  const Hand_Location &proto_sequence_loc = candidates_work[cn].proto_sequence_loc[gn];
  return cand_graph_sub_open_meld_loc[proto_sequence_loc.get_first()]
                                     [proto_sequence_loc.get_second()];
}

const std::array<int, 3> &Hand_Calculator_Work::get_const_win_loc(const int cn,
                                                                  const int gn) const {
  const Hand_Location &proto_sequence_loc = candidates_work[cn].proto_sequence_loc[gn];
  return win_graph_loc[proto_sequence_loc.get_first()][proto_sequence_loc.get_second()];
}