#pragma once

#include "../share/calc_shanten.hpp"
#include "../share/include.hpp"
#include "combination.hpp"
#include "full_defense.hpp"
#include "hand_action.hpp"
#include "hand_calculator_work.hpp"
#include "hand_group.hpp"

int mod2(const int x);

bool same_element_exist(const std::vector<int> &vec1, const std::vector<int> &vec2);

Tile_Array cal_using_array(const Tile_Array &hand, const Open_Meld_Vector &open_meld);

void set_tsumo_node_exec(
    Hand_Change &hand_change_new, bool is_in0,
    boost::container::static_vector<Hand_Group, MAX_CANDIDATES_NUM> &candidates_work,
    boost::unordered_set<Hand_Change> &tcs, int &in0num);

void set_tsumo_node_child(
    const Bit_Tile_Num &bit_tile_num, const int hand_all_num, std::vector<int> &tile_in_pattern_nip,
    std::vector<int> &remain, int nin,
    boost::container::static_vector<Hand_Group, MAX_CANDIDATES_NUM> &candidates_work,
    boost::unordered_set<Hand_Change> &tcs, int &in0num);

void set_tsumo_node_meld(
    const Bit_Tile_Num &bit_tile_num, const int hand_all_num, Hand_Analyzer proto_sequence_in,
    const int open_meld_cand_tile, const int in_num_end,
    boost::container::static_vector<Hand_Group, MAX_CANDIDATES_NUM> &candidates_work,
    boost::unordered_set<Hand_Change> &tcs, int &in0num);

void set_tenpai_prob_other(const int my_pid, const Game_State &game_state, const int tsumo_num,
                           const std::array<float, 4> &tenpai_prob_now, double **tenpai_prob_other,
                           double **riichi_tenpai_prob_other);

float cal_other_end_prob(const bool my_riichi, const int act_num1, const int act_num2,
                         const int other_riichi_num, const float other_end_prob_input);
float cal_tenpai_after_prob(const bool my_riichi, const int act_num1, const int act_num2,
                            const float tenpai_prob_now);
void set_other_end_prob(const int my_pid, const int tsumo_num, const int act_num,
                        const std::array<float, 4> &tenpai_prob_now, double *other_end_prob,
                        double *riichi_other_end_prob, const Game_State &game_state);

void set_exp_drawn_round_DP(
    const int my_pid, const double *const *tenpai_prob_other,
    const double *const *riichi_tenpai_prob_other,
    const std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> &drawn_round_pt_exp,
    const std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> &drawn_round_pt_exp_ar,
    double exp_drawn_round[2], double &exp_drawn_round_ar);

double cal_fold_exp(const int my_pid, const int cn, const int gn, const int tn, const float wa[3],
                    Hand_Calculator_Work &hand_calculator_work, double **deal_in_p_tile,
                    double **deal_in_e_tile, double exp_other, const int tsumo_num,
                    const int fold_choice_mode, double drawn_round_prob,
                    double not_ready_drawn_round_value, const Tile_Array hand_kind,
                    const Open_Meld_Vector open_meld_kind);

float get_coeff_for_ron_DP(const int tile, const int my_pid,
                           const Hand_Analyzer_Basic &proto_sequence, const int pon_ron[38],
                           const int is_ron[38], const double my_tenpai_prob,
                           const std::array<std::array<float, 38>, 4> &deal_in_tile_prob,
                           double **tenpai_prob_other, const int tn,
                           const float riichi_ron_ratio_para[5],
                           const float not_riichi_ron_ratio_para[5],
                           const float hidden_tenpai_ron_ratio[6]);

void exec_calc_DP(const double riichi_regression_coeff, const int fold_choice_mode,
                  const int my_pid, const int tsumo_num, Hand_Calculator_Work &hand_calculator_work,
                  const double drawn_round_prob_now, const double exp_drawn_round[2],
                  const double exp_drawn_round_ar, const double exp_drawn_round_if_open_meld[2],
                  const bool is_last_mode, double **deal_in_p_tile, double **riichi_deal_in_p_tile,
                  double **deal_in_e_tile, double **riichi_deal_in_e_tile, double *other_end_prob,
                  double *riichi_other_end_prob, const double exp_other, const double exp_other_ar,
                  const double exp_other_kan, const double my_tenpai_prob,
                  const std::array<std::array<float, 38>, 4> &deal_in_tile_prob,
                  double **tenpai_prob_other, const Game_State &game_state, const Tactics &tactics);
