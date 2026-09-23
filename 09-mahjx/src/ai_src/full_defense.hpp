#pragma once

#include "../share/include.hpp"
#include "../share/types.hpp"
#include "hand_analyzer.hpp"

double pow_double(const double x, const int y);

class Tile_Risk {
 public:
  Tile_Risk();
  bool operator<(const Tile_Risk &rhs) const;

  int tile;
  int tile_num;
  double deal_in_prob;
  double deal_in_value;
  double risk_coeff;

  void set_value_prob(int tile_in, int tile_num_in, double deal_in_prob_in, double deal_in_decay);
  void set_value_exp(int tile_in, int tile_num_in, double deal_in_prob_in, double deal_in_value_in,
                     double other_value_in, double deal_in_decay);
};

class Full_Defense {
 public:
  Full_Defense();
  Tile_Array hand;
  double deal_in_decay;
  double other_value, not_ready_drawn_round_value;
  std::array<double, 38> deal_in_tile_prob, deal_in_tile_value;

  std::vector<Tile_Risk> tile_risk;
  double full_defense_exp, full_defense_deal_in_prob;

  void set_hand(const Tile_Array &hand_in);
  void set_hand_proto_sequence(const Hand_Analyzer_Basic &hand_analyzer);
  void set_condition(const std::array<float, 38> &deal_in_tile_prob_in,
                     const std::array<float, 38> &deal_in_tile_value_in,
                     const double other_value_in, const double not_ready_drawn_round_value_in);

  void set_full_defense_value(const int fold_num);
  void modify_full_defense_value_with_drawn_round(const int open_meld_num, const float w[3],
                                                  const float drawn_round_prob);
};

Full_Defense cal_full_defense(const Tile_Array &hand_in,
                              const std::array<float, 38> &deal_in_tile_prob_in,
                              const std::array<float, 38> &deal_in_tile_value_in,
                              const double other_value_in,
                              const double not_ready_drawn_round_value_in, const int fold_num);
