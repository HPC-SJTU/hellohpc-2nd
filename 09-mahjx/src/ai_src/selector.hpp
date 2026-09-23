#pragma once

#include "../share/include.hpp"
#include "../share/make_move.hpp"
#include "../share/types.hpp"
#include "../share/win_points.hpp"
#include "expected_values.hpp"
#include "hand_action.hpp"
#include "hand_calculator.hpp"
#include "mahjong_util.hpp"
#include "turn_calc.hpp"

struct Review {
  bool has_total_deal_in_tile_prob_now = false;
  float total_deal_in_tile_prob_now = 0;
  bool has_total_deal_in_tile_weighted_utility_now = false;
  float total_deal_in_tile_weighted_utility_now = 0;
  bool has_pt_exp_after = false;
  float pt_exp_after = 0;
  bool has_pt_exp_total = false;
  float pt_exp_total = 0;

  void set_total_deal_in_tile_prob_now(float value) {
    has_total_deal_in_tile_prob_now = true;
    total_deal_in_tile_prob_now = value;
  }
  void set_total_deal_in_tile_weighted_utility_now(float value) {
    has_total_deal_in_tile_weighted_utility_now = true;
    total_deal_in_tile_weighted_utility_now = value;
  }
  void set_pt_exp_after(float value) {
    has_pt_exp_after = true;
    pt_exp_after = value;
  }
  void set_pt_exp_total(float value) {
    has_pt_exp_total = true;
    pt_exp_total = value;
  }
};

struct Decision_Score {
  Moves moves;
  Review review;
};

class Tile_Choice {
 public:
  Action_Type action_type;
  // This stores the action after a tsumo. It is one of AT_DISCARD, AT_RIICHI_DECLARE,
  // AT_CONCEALED_KAN, AT_UPGRADED_KAN, AT_TSUMO_WIN, or AT_NINE_TERMINALS. For AT_RIICHI_DECLARE,
  // set the discard as well.
  int tile;
  float pt_exp_after;
  float pt_exp_total;
  float pt_exp_after_fold;

  Review review;

  Tile_Choice();
  bool operator<(const Tile_Choice &rhs) const;
  int is_choice_riichi_declare();
  Moves get_moves(const Game_State &game_state, const int my_pid, const int tsumo_tile) const;
};

class Open_Meld_Choice {
 public:
  int open_meld_tile;
  Action_Type open_meld_action_type;
  int tile_out;
  int exposed_tile[3];

  float pt_exp_after;
  float pt_exp_after_prev;
  float pt_exp_total;
  float pt_exp_total_prev;

  Review review;

  Open_Meld_Choice();
  void reset();
  bool operator<(const Open_Meld_Choice &rhs) const;
  Moves get_moves(const int my_pid, const int target) const;
};

std::array<std::array<std::array<std::array<float, 12>, 14>, 4>, 4> cal_round_end_pt_exp(
    const Moves &game_record, const Game_State &game_state, const int my_pid,
    const bool riichi_mode);
std::array<std::array<std::array<std::array<float, 2>, 2>, 2>, 2> cal_drawn_round_pt_exp(
    const Moves &game_record, const Game_State &game_state, const int my_pid,
    const bool riichi_mode);
int cal_tsumo_num_DP(const Moves &game_record, const int my_pid);

void set_exposed_tile(const Hand_State2 &dst_hand_state, const Open_Meld_Vector &present_open_meld,
                      const int current_tile, int exposed_tile[3]);
int isolated_tile_most_needless(const Tile_Array &hand, const Tile_Array &visible,
                                const int round_wind, const int self_wind,
                                const std::vector<int> &dora_marker);

class Selector {
 public:
  std::vector<Tile_Choice> tile_choice;
  std::vector<Open_Meld_Choice> open_meld_choice;

  Selector();
  void set_selector(const Moves &game_record, const int my_pid, const Tactics &tactics);
};

Moves ai(const Moves &game_record, const int pid, const bool console_out_input);

std::vector<std::pair<Moves, float>> calc_moves_score(const Moves &game_record, const int pid);

std::vector<Decision_Score> ai_review(const Moves &game_record, const int pid);

void set_tactics(const std::array<Tactics, 4> &tactics);
