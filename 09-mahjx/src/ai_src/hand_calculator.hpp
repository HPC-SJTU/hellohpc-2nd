#pragma once

#include "../share/include.hpp"
#include "full_defense.hpp"
#include "hand_action.hpp"
#include "hand_calculator_supplement.hpp"
#include "hand_calculator_work.hpp"
#include "hand_group.hpp"
#include "tactics.hpp"

class Hand_Calculator {
 public:
  Hand_Calculator();
  Hand_Calculator(int my_pid_in);

  void reset(int my_pid_in, bool has_red_dora_in);

  int tiles_in[38];
  int tiles_out[38];
  int effective[38];
  std::vector<Hand_Group> candidates_sub;

  boost::unordered_set<Hand_Change> tcs;
  boost::unordered_set<Hand_Change> tcs_sub;

  int my_pid;
  bool has_red_dora;
  int hand_all_num;
  Bit_Tile_Num bit_tile_num;
  int in0num;
  int in0num_sub;
  int open_meld_cand_tile;
  int cnm_restrict;
  std::vector<int> cnm_restriction;

  double rn_para;

  void set_in_out(Hand_Analyzer proto_sequence_in);
  void get_effective(const Game_State &game_state, Hand_Analyzer hand_analyzer);

  const Hand_Analyzer_Basic &get_const_proto_sequence(const Hand_Location loc) const;
  const Hand_Analyzer_Basic &get_const_proto_sequence_cgn(const int cn, const int gn) const;
  Hand_Analyzer_Basic &get_ref_proto_sequence_cgn(const int cn, const int gn);

  size_t candidates_size() const;
  size_t group_size(const int cn) const;

  double get_win_prob(const int cn, const int gn, const int tn) const;
  std::array<double, 5> get_win_han_prob(const int cn, const int gn, const int tn) const;
  double get_win_exp(const int cn, const int gn, const int tn) const;
  double get_tenpai_prob(const int cn, const int gn, const int tn) const;
  double get_points_exp(const int cn, const int gn, const int tn) const;
  double get_points_gain(const int cn, const int gn, const int tn) const;
  double get_fold_exp(const int cn, const int gn, const int tn) const;

  // merge_candidates is unused now. It is planned for use when candidate enumeration takes too
  // long.
  void merge_candidates_child(std::vector<Hand_Group> &cand1,
                              boost::unordered_set<Hand_Change> &tcs1,
                              std::vector<Hand_Group> &cand2);

  void merge_candidates(std::vector<std::vector<Hand_Group>> &candvv,
                        std::vector<boost::unordered_set<Hand_Change>> &tcsv);

  void set_tsumo_node_1out(const Hand_Analyzer &proto_sequence_in);

  void set_tsumo_node_1in_1out(const Game_State &game_state,
                               const Hand_Analyzer_Basic &proto_sequence_in,
                               Hand_Change &hand_change_tmp_new);

  void set_tsumo_node_seven_pairs(Hand_Analyzer proto_sequence_in, int in_num_end);

  void set_tsumo_edge(Hand_Change hand_change_tmp, const int thread_num);

  void add_open_meld_node(const Game_State &game_state, int cn_start, int cn_end,
                          int open_meld_num_begin, int open_meld_num_end, int must_effective,
                          int open_meld_must, boost::unordered_map<Hand_State2, int> *ts_maps);

  void set_candidates3_sub(Hand_Analyzer hand_analyzer, const int change_num);
  void set_candidates3_single_thread(const Game_State &game_state, Hand_Analyzer hand_analyzer,
                                     Hand_Analyzer hand_analyzer_af, const int cn_max,
                                     const Tactics &tactics);
  void set_candidates3_multi_thread(const Moves &game_record, const Game_State &game_state,
                                    const std::array<bool, 38> discard, Hand_Analyzer hand_analyzer,
                                    const Tactics &tactics);

  void set_cand_graph_sub_child(int cn1, int gn1, int cn2, int tile_in, int tile_out,
                                Action_Type action_id, int tile0, int tile1, int tile2,
                                boost::unordered_map<Hand_State2, int> *ts_maps,
                                const int thread_num);
  void set_cand_graph_sub(const int cn1, const std::vector<std::array<int, 3>> &tsumo_edge_array,
                          boost::unordered_map<Hand_State2, int> *ts_maps, const int thread_num);

  int get_max_group_size();
  int get_max_edge_size();
  int get_sub_num_all();

  void set_win_shanten_num(const Game_State &game_state);
  int get_open_meld_win_shanten_num(const Game_State &game_state, const Tile_Array &current_hand,
                                    const bool chii_action);

  void set_win_exp(
      const Game_State &game_state,
      const std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> &round_end_pt_exp);

  void calc_win_prob(
      const int tsumo_num, const double exp_min, const Game_State &game_state,
      const std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> &round_end_pt_exp);

  void calc_DP(
      const int act_num, const int tsumo_num, const double exp_other, const double exp_other_ar,
      const double exp_other_kan, const std::array<float, 4> &tenpai_prob_now,
      const std::array<std::array<float, 38>, 4> &deal_in_tile_prob,
      const std::array<std::array<float, 38>, 4> &deal_in_tile_value, const int ori_choice_mode,
      const double riichi_regression_coeff, double my_tenpai_prob, double drawn_round_prob_now,
      const Tile_Array &tile_visible_kind, const Game_State &game_state,
      const std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> &round_end_pt_exp,
      const std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> &exp_drawn_round,
      const std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> &exp_drawn_round_ar,
      const Tactics &tactics);
};
