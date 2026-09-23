#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>

namespace sparsemax_bellman {
constexpr double kTauPoints = 0.00048828125;
struct Record {
  double win_prob, win_exp, tenpai_prob, points_exp;
};
struct Candidate {
  Record record;
  double coeff;
  double event_weight;
  Candidate() : record{0, 0, 0, 0}, coeff(1), event_weight(1) {}
  Candidate(const Record &r, double c = 1, double event = 1)
      : record(r), coeff(c), event_weight(event) {}
};

inline std::vector<double> weights(const std::vector<Candidate> &c) {
  std::vector<double> p(c.size(), 0.0);
  if (c.empty()) return p;
  double max_score = c[0].record.points_exp;
  for (std::size_t i = 1; i < c.size(); ++i)
    max_score = std::max(max_score, c[i].record.points_exp);
  std::vector<std::size_t> order(c.size());
  for (std::size_t i = 0; i < c.size(); ++i) {
    order[i] = i;
    for (std::size_t j = i; j && c[order[j]].record.points_exp > c[order[j - 1]].record.points_exp;
         --j)
      std::swap(order[j], order[j - 1]);
  }
  double prefix = 0.0, theta = 0.0;
  for (std::size_t k = 0; k < order.size(); ++k) {
    const double z = (c[order[k]].record.points_exp - max_score) / kTauPoints;
    prefix += z;
    const double t = (prefix - 1.0) / double(k + 1);
    if (z > t) theta = t;
  }
  for (std::size_t i = 0; i < c.size(); ++i)
    p[i] = std::max(0.0, (c[i].record.points_exp - max_score) / kTauPoints - theta);
  return p;
}
inline Record mix_all(const std::vector<Candidate> &c) {
  Record out{0, 0, 0, 0};
  const std::vector<double> p = weights(c);
  for (std::size_t i = 0; i < c.size(); ++i) {
    out.win_prob += p[i] * c[i].record.win_prob;
    out.win_exp += p[i] * c[i].record.win_exp;
    out.tenpai_prob += p[i] * c[i].record.tenpai_prob;
    out.points_exp += p[i] * c[i].record.points_exp;
  }
  return out;
}
// Apply an action-dependent coefficient after the simplex decision.  This is
// deliberately not an average of discrete flags: p_i is the probability of
// choosing record i, and coeff_i is applied to that record's Bellman delta.
inline Record mix_delta(const std::vector<Candidate> &c, const Record &baseline) {
  Record out{0, 0, 0, 0};
  const std::vector<double> p = weights(c);
  for (std::size_t i = 0; i < c.size(); ++i) {
    const double w = p[i] * c[i].coeff * c[i].event_weight;
    out.win_prob += w * (c[i].record.win_prob - baseline.win_prob);
    out.win_exp += w * (c[i].record.win_exp - baseline.win_exp);
    out.tenpai_prob += w * (c[i].record.tenpai_prob - baseline.tenpai_prob);
    out.points_exp += w * (c[i].record.points_exp - baseline.points_exp);
  }
  return out;
}
inline void assign(const Record &r, double &wp, double &we, double &tp, double &pe) {
  wp = r.win_prob;
  we = r.win_exp;
  tp = r.tenpai_prob;
  pe = r.points_exp;
}
}  // namespace sparsemax_bellman
