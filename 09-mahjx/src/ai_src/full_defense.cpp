#include "full_defense.hpp"

#include <cstdio>

double pow_double(const double x, const int y) {
  double res = 1.0;
  for (int i = 0; i < y; i++) {
    res = res * x;
  }
  return res;
}

Tile_Risk::Tile_Risk() {}

bool Tile_Risk::operator<(const Tile_Risk &rhs) const {
  if (risk_coeff < rhs.risk_coeff) {
    return true;
  } else if (risk_coeff > rhs.risk_coeff) {
    return false;
  }

  if (tile_num < rhs.tile_num) {
    return true;
  } else {
    return false;
  }
}

void Tile_Risk::set_value_prob(int tile_in, int tile_num_in, double deal_in_prob_in,
                               double deal_in_decay) {
  tile = tile_in;
  tile_num = tile_num_in;
  deal_in_prob = deal_in_prob_in;
  double beta = 1.0 - pow_double(deal_in_decay, tile_num);
  risk_coeff = deal_in_prob / (1.0 - deal_in_prob) / beta;
}

void Tile_Risk::set_value_exp(int tile_in, int tile_num_in, double deal_in_prob_in,
                              double deal_in_value_in, double other_value, double deal_in_decay) {
  tile = tile_in;
  tile_num = tile_num_in;
  deal_in_prob = deal_in_prob_in;
  deal_in_value = deal_in_value_in;
  double beta = 1.0 - pow_double(deal_in_decay, tile_num);
  risk_coeff =
      deal_in_prob * (other_value - deal_in_value) / (deal_in_prob + beta - deal_in_prob * beta);
}

Full_Defense::Full_Defense() { deal_in_decay = 0.9; }

void Full_Defense::set_hand(const Tile_Array &hand_in) {
  for (int tile = 0; tile < 38; tile++) {
    hand[tile] = hand_in[tile];
  }
  for (int c = 0; c < 3; c++) {
    if (hand[10 * c + 5] > 0 && hand[10 * c + 10] > 0) {
      hand[10 * c + 5] += hand[10 * c + 10];
      hand[10 * c + 10] = 0;
    }
  }
}

void Full_Defense::set_hand_proto_sequence(const Hand_Analyzer_Basic &hand_analyzer) {
  hand[0] = 0;
  for (int tile = 1; tile < 38; tile++) {
    hand[tile] = hand_analyzer.count_tile(tile);
  }
  for (int c = 0; c < 3; c++) {
    if (hand[10 * c + 5] > 0 && hand[10 * c + 10] > 0) {
      hand[10 * c + 5] += hand[10 * c + 10];
      hand[10 * c + 10] = 0;
    }
  }
}

void Full_Defense::set_condition(const std::array<float, 38> &deal_in_tile_prob_in,
                                 const std::array<float, 38> &deal_in_tile_value_in,
                                 const double other_value_in,
                                 const double not_ready_drawn_round_value_in) {
  other_value = other_value_in;
  not_ready_drawn_round_value = not_ready_drawn_round_value_in;
  for (int tile = 0; tile < 38; tile++) {
    deal_in_tile_prob[tile] = deal_in_tile_prob_in[tile];
    deal_in_tile_value[tile] = deal_in_tile_value_in[tile];
  }
}

void Full_Defense::set_full_defense_value(const int fold_num) {
  tile_risk.clear();
  Tile_Risk tile_risk_tmp;
  for (int tile = 1; tile < 38; tile++) {
    if (hand[tile] > 0) {
      tile_risk_tmp.set_value_exp(tile, hand[tile], deal_in_tile_prob[tile],
                                  deal_in_tile_value[tile], other_value, deal_in_decay);
      tile_risk.push_back(tile_risk_tmp);
    }
  }

  std::sort(tile_risk.begin(), tile_risk.end());
  int num = 0;
  full_defense_deal_in_prob = 0.0;
  full_defense_exp = 0.0;
  double coeff_tmp = 1.0;
  for (int n = 0; n < tile_risk.size(); n++) {
    if (num >= fold_num) {
      break;
    }
    full_defense_deal_in_prob += coeff_tmp * deal_in_tile_prob[tile_risk[n].tile];
    full_defense_exp +=
        coeff_tmp * deal_in_tile_prob[tile_risk[n].tile] * deal_in_tile_value[tile_risk[n].tile];
    coeff_tmp = coeff_tmp * (1.0 - deal_in_tile_prob[tile_risk[n].tile]) *
                pow_double(deal_in_decay, hand[tile_risk[n].tile]);
    num += tile_risk[n].tile_num;
  }
  full_defense_exp += (1.0 - full_defense_deal_in_prob) * other_value;
}

void Full_Defense::modify_full_defense_value_with_drawn_round(const int open_meld_num,
                                                              const float w[3],
                                                              const float drawn_round_prob) {
  if (full_defense_deal_in_prob > 0.0) {
    full_defense_exp -= (1.0 - full_defense_deal_in_prob) * other_value;
    float x[3];
    x[0] = 1.0;
    x[1] = open_meld_num;
    x[2] = my_logit(full_defense_deal_in_prob);
    float prob_mod = logistic(w, x, 3);
    full_defense_exp *= prob_mod / full_defense_deal_in_prob;
    full_defense_deal_in_prob = prob_mod;
    full_defense_exp +=
        (1.0 - full_defense_deal_in_prob) *
        (drawn_round_prob * not_ready_drawn_round_value + (1.0 - drawn_round_prob) * other_value);
  } else {
    full_defense_exp =
        drawn_round_prob * not_ready_drawn_round_value + (1.0 - drawn_round_prob) * other_value;
  }
}

Full_Defense cal_full_defense(const Tile_Array &hand_in,
                              const std::array<float, 38> &deal_in_tile_prob_in,
                              const std::array<float, 38> &deal_in_tile_value_in,
                              const double other_value_in,
                              const double not_ready_drawn_round_value_in, const int fold_num) {
  Full_Defense full_defense;
  full_defense.set_hand(hand_in);
  full_defense.set_condition(deal_in_tile_prob_in, deal_in_tile_value_in, other_value_in,
                             not_ready_drawn_round_value_in);
  full_defense.set_full_defense_value(fold_num);
  return full_defense;
}
