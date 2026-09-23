#pragma once

#include <string>
#include <vector>

#include "share/event.hpp"

struct Decision_Metric {
  Decision_Metric() : present(false), value(0.0f) {}
  explicit Decision_Metric(float value_in) : present(true), value(value_in) {}

  bool present;
  float value;
};

struct Decision {
  Moves actions;
  float score = 0.0f;
  Decision_Metric pt_exp_after;
  Decision_Metric total_deal_in_tile_prob_now;
  Decision_Metric total_deal_in_tile_weighted_utility_now;
};

struct Analysis_Position {
  int trigger_eid = -1;
  int player = -1;
  std::vector<Decision> candidates;
  Moves actual;
  int actual_candidate = -1;
};

// Discard normalization may change from_draw without changing the tile decision.
inline bool analysis_same_action(const Event &a, const Event &b) {
  if (a.type != b.type || a.player != b.player || a.tile != b.tile) return false;
  if (a.type == EventType::DISCARD) return true;
  return a.target == b.target && a.from_draw == b.from_draw && a.consumed == b.consumed;
}

inline bool analysis_same_moves(const Moves &a, const Moves &b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (!analysis_same_action(a[i], b[i])) return false;
  return true;
}

Analysis_Position analyze_position(const Moves &prefix, int player, int trigger_eid);
