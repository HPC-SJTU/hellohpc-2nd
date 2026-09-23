#include "analyze.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

#include "ai_src/selector.hpp"
#include "analysis_io.hpp"

Analysis_Position analyze_position(const Moves &prefix, int player, int trigger_eid) {
  Analysis_Position p;
  p.trigger_eid = trigger_eid;
  p.player = player;
  std::map<std::string, Decision> unique;
  for (const Decision_Score &item : ai_review(prefix, player)) {
    if (!item.review.has_pt_exp_total) continue;
    Decision d;
    d.actions = item.moves;
    d.score = item.review.pt_exp_total;
    if (item.review.has_pt_exp_after) d.pt_exp_after = Decision_Metric(item.review.pt_exp_after);
    if (item.review.has_total_deal_in_tile_prob_now)
      d.total_deal_in_tile_prob_now = Decision_Metric(item.review.total_deal_in_tile_prob_now);
    if (item.review.has_total_deal_in_tile_weighted_utility_now)
      d.total_deal_in_tile_weighted_utility_now =
          Decision_Metric(item.review.total_deal_in_tile_weighted_utility_now);
    const std::string key = canonical_action_text(d.actions);
    if (unique.find(key) == unique.end() || d.score > unique[key].score) unique[key] = d;
  }
  for (const std::pair<const std::string, Decision> &x : unique) p.candidates.push_back(x.second);
  return p;
}
