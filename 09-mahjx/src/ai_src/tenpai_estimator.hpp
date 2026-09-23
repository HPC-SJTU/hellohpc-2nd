#pragma once

#include "../share/include.hpp"
#include "../share/riichi_rules.hpp"
#include "../share/types.hpp"
#include "mahjong_util.hpp"
#include "tactics.hpp"
#include "tenpai_probability_calc.hpp"

bool value_tile_check_est(const int value_tile, const Open_Meld_Vector &open_meld,
                          const int triplet[38], const int wait_tile, const Wait_Type wait_type);
bool full_flush_check_est(const Open_Meld_Vector &open_meld, const int head[38],
                          const int triplet[38], const int sequence[30], const int wait_tile);
bool half_flush_check_est(const Open_Meld_Vector &open_meld, const int head[38],
                          const int triplet[38], const int sequence[30], const int wait_tile);
bool simples_check_est(const Open_Meld_Vector &open_meld, const int head[38], const int triplet[38],
                       const int sequence[30], const int wait_tile, const Wait_Type wait_type);
bool terminals_and_honors_check_est(const Open_Meld_Vector &open_meld, const int head[38],
                                    const int triplet[38], const int sequence[30],
                                    const int wait_tile, const Wait_Type wait_type);
bool suitsu_check_est(const Open_Meld_Vector &open_meld, const int head[38], const int triplet[38],
                      const int sequence[30], const int wait_tile, const Wait_Type wait_type);
bool pure_outside_hand_check_est(const Open_Meld_Vector &open_meld, const int head[38],
                                 const int triplet[38], const int sequence[30], const int wait_tile,
                                 const Wait_Type wait_type);
bool outside_hand_check_est(const Open_Meld_Vector &open_meld, const int head[38],
                            const int triplet[38], const int sequence[30], const int wait_tile,
                            const Wait_Type wait_type);
bool all_triplets_check_est(const Open_Meld_Vector &open_meld, const int head[38],
                            const int triplet[38], const int sequence[30], const int wait_tile,
                            const Wait_Type wait_type);
bool three_color_triplet_check_est(const Open_Meld_Vector &open_meld, const int head[38],
                                   const int triplet[38], const int sequence[30],
                                   const int wait_tile, const Wait_Type wait_type);
bool three_color_sequence_check_est(const Open_Meld_Vector &open_meld, const int head[38],
                                    const int triplet[38], const int sequence[30],
                                    const int wait_tile, const Wait_Type wait_type,
                                    const int open_wait_id);
bool full_straight_check_est(const Open_Meld_Vector &open_meld, const int head[38],
                             const int triplet[38], const int sequence[30], const int wait_tile,
                             const Wait_Type wait_type, const int open_wait_id);
int concealed_triplet_num_count_est(const Open_Meld_Vector &open_meld, const int triplet[38]);
int kan_num_count_est(const Open_Meld_Vector &open_meld);

class Win_Estimate_Info {
 public:
  int tile;
  int han;
  int fu;
  Win_Estimate_Info();
};

Win_Estimate_Info yaku_check(const int round_wind, const int self_wind,
                             const std::vector<int> &dora_vector, const int visible_dora_num,
                             const Tile_Array hand, const int head[38], const int triplet[38],
                             const int sequence[30], const int wait_tile, const Wait_Type wait_type,
                             const int open_wait_id);

class Hand_Estimator_Element {
 public:
  float prob, prob_now;
  unsigned long long weight_units;
  bool admitted_now;
  Tile_Array hand;
  std::vector<Win_Estimate_Info> win_vector;

  Hand_Estimator_Element();
  Hand_Estimator_Element(Tile_Array hand_in);
  void reset(Tile_Array hand_in);
  Color_Type get_flushing_color(const Open_Meld_Vector &open_meld);
};

class Hand_Estimator {
 private:  // Working data.
  int hand_max_num;
  int visible_dora_num;
  Tile_Array remaining;
  std::array<bool, 38> decline_win;

 public:
  Hand_Estimator();

 private:
  void tenpai_check(const int round_wind, const int self_wind, const std::vector<int> &dora_vector,
                    const Open_Meld_Vector &open_meld, int type, Tile_Array &hand, int head[38],
                    int triplet[38], int sequence[30],
                    std::vector<Hand_Estimator_Element> &hand_expected_value);
  void add_sequence(const int round_wind, const int self_wind, const std::vector<int> &dora_vector,
                    const Open_Meld_Vector &open_meld, int head_num, int triplet_num,
                    int sequence_num, int start, Tile_Array &hand, int head[38], int triplet[38],
                    int sequence[30], std::vector<Hand_Estimator_Element> &hand_expected_value);
  void add_triplet(const int round_wind, const int self_wind, const std::vector<int> &dora_vector,
                   const Open_Meld_Vector &open_meld, int head_num, int triplet_num, int start,
                   Tile_Array &hand, int head[38], int triplet[38], int sequence[30],
                   std::vector<Hand_Estimator_Element> &hand_expected_value);
  void add_head(const int round_wind, const int self_wind, const std::vector<int> &dora_vector,
                const Open_Meld_Vector &open_meld, int head_num, int start, Tile_Array &hand,
                int head[38], int triplet[38], int sequence[30],
                std::vector<Hand_Estimator_Element> &hand_expected_value);
  std::vector<Hand_Estimator_Element> cal_hand_expected_value(
      const int round_wind, const int self_wind, const std::vector<int> &dora_marker,
      const Open_Meld_Vector &open_meld, const int hand_max_num_in, const Tile_Array &remaining_in,
      const std::array<bool, 38> &decline_win_in);

 public:
  std::vector<Hand_Estimator_Element> cal_hand_expected_value_with_prob(
      const Moves &game_record, const Game_State &game_state, const int target,
      const int hand_max_num_in, const Tile_Array &remaining_in,
      const std::array<bool, 38> &decline_win_in);

  void normalize2(const Open_Meld_Vector &open_meld, const float tenpai_prob,
                  const float normal_tenpai_prob, const std::array<float, 3> flushing_tenpai_prob,
                  std::vector<Hand_Estimator_Element> &hand_expected_value);
};

std::array<std::array<std::array<float, 12>, 14>, 38> cal_tile_prob_from_hand_expected_value(
    const std::vector<Hand_Estimator_Element> &hand_expected_value, const bool is_tsumo,
    const bool is_now);

class Wait_Coeff {
 public:
  Wait_Coeff();
  std::array<float, 4> shape_prob;
  std::array<float, 38> pair_wait_coeff;
  std::array<float, 38> dual_pair_coeff;
  std::array<std::array<float, 7>, 3> open_wait_coeff;
  std::array<std::array<float, 9>, 3> kanchan_coeff;
  std::array<std::array<float, 2>, 3> edge_wait_coeff;

  void init_coeff(const int my_pid);
  void set_shape_prob(const int my_pid);
  void safe_flag_to_coeff(const std::array<bool, 38> &safe);
  void visible_to_coeff(const Tile_Array &visible_all, const Tile_Array &visible);
  void discard_before_riichi_to_coeff(const Game_State &game_state, const int my_pid,
                                      const int target);
  void ratio_proto_sequence_to_coeff(const Game_State &game_state, const int my_pid,
                                     const int target);
  void std_coeff();
};

std::array<std::array<std::array<float, 12>, 14>, 38> cal_tile_prob_from_wait_coeff(
    const Game_State &game_state, const Wait_Coeff &wait_coeff,
    const std::array<std::array<double, 12>, 14> &hanfu_weight, const bool is_tsumo);

std::array<std::array<float, 12>, 14> cal_win_hanfu_prob(
    const std::array<std::array<std::array<float, 12>, 14>, 38> &tile_prob);
Tile_Array sum_tile_array(const Tile_Array &tile_array1, const Tile_Array &tile_array2);
Tile_Array cal_remaining_kind_array(const Tile_Array &visible_kind);
std::array<bool, 38> get_discard_kind(const River &river);

class Tenpai_Estimator_Simple {
 public:
  double tenpai_prob;  // Current tenpai probability.
  std::array<std::array<std::array<float, 12>, 14>, 38>
      tile_tsumo_prob;  // The probability of each tile, han, and fu index for a tsumo win.
  std::array<std::array<std::array<float, 12>, 14>, 38>
      tile_ron_prob;  // The probability of each tile, han, and fu index for a ron win.
  std::array<std::array<std::array<float, 12>, 14>, 38>
      tile_ron_prob_now;  // The probability of each tile, han, and fu index for the current
                          // discard.

  Tenpai_Estimator_Simple();
  void set_tenpai2(const Moves &game_record, const Game_State &game_state, const int my_pid,
                   const int target);
  void set_tenpai_estimator(const Moves &game_record, const Game_State &game_state,
                            const int my_pid, const int target, const Tactics &tactics);
};

std::array<float, 4> get_tenpai_prob_array(
    const std::array<Tenpai_Estimator_Simple, 4> &tenpai_estimator);
std::array<std::array<std::array<float, 12>, 14>, 4> cal_win_hanfu_prob_array(
    const std::array<Tenpai_Estimator_Simple, 4> &tenpai_estimator, const bool is_tsumo);

std::pair<std::array<std::array<float, 38>, 4>, std::array<std::array<float, 38>, 4>>
cal_deal_in_tile_prob_value(
    const std::array<Tenpai_Estimator_Simple, 4> &tenpai_estimator,
    const std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> &round_end_pt_exp,
    const int my_pid, const bool is_now);

struct TotalDealIn {
  std::array<float, 38> probability;
  std::array<float, 38> value;
  std::array<float, 38> weighted_utility;
};
TotalDealIn cal_total_deal_in_tile_prob_value(
    const int my_pid, const std::array<Tenpai_Estimator_Simple, 4> &tenpai_estimator,
    const std::array<std::array<float, 38>, 4> &deal_in_tile_prob,
    const std::array<std::array<float, 38>, 4> &deal_in_tile_value);
