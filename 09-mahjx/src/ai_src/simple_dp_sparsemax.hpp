#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

// This operator is deliberately separate from the full-DP operator.  In
// particular, the simplex score is points_exp only; the other fields are
// payload and must not affect projection.
namespace simple_dp_sparsemax {
struct PointsRecord {
  double win_prob, points_exp;
  std::array<double, 5> han;
};
struct Candidate {
  PointsRecord record;
  double coeff, event_weight;
  Candidate() : record{0, 0, {{0, 0, 0, 0, 0}}}, coeff(1), event_weight(1) {}
  Candidate(const PointsRecord &r, double c = 1, double e = 1)
      : record(r), coeff(c), event_weight(e) {}
};
inline std::vector<double> weights(const std::vector<Candidate> &c) {
  std::vector<double> p(c.size(), 0);
  if (c.empty()) return p;
  double m = c[0].record.points_exp;
  for (std::size_t i = 1; i < c.size(); ++i) m = std::max(m, c[i].record.points_exp);
  double theta = 0, prefix = 0;
  std::vector<std::size_t> order(c.size());
  for (std::size_t i = 0; i < c.size(); ++i) {
    order[i] = i;
    for (std::size_t j = i; j && c[order[j]].record.points_exp > c[order[j - 1]].record.points_exp;
         --j)
      std::swap(order[j], order[j - 1]);
  }
  for (std::size_t k = 0; k < order.size(); ++k) {
    double z = c[order[k]].record.points_exp - m;
    prefix += z;
    double t = (prefix - 1) / double(k + 1);
    if (z > t) theta = t;
  }
  for (std::size_t i = 0; i < c.size(); ++i)
    p[i] = std::max(0.0, c[i].record.points_exp - m - theta);
  return p;
}
inline PointsRecord mix(const std::vector<Candidate> &c) {
  PointsRecord out{0, 0, {{0, 0, 0, 0, 0}}};
  auto p = weights(c);
  for (std::size_t i = 0; i < c.size(); ++i) {
    out.win_prob += p[i] * c[i].record.win_prob;
    out.points_exp += p[i] * c[i].record.points_exp;
    for (int h = 0; h < 5; ++h) out.han[h] += p[i] * c[i].record.han[h];
  }
  return out;
}
inline PointsRecord mix_delta(const std::vector<Candidate> &c, const PointsRecord &b) {
  PointsRecord out{0, 0, {{0, 0, 0, 0, 0}}};
  auto p = weights(c);
  for (std::size_t i = 0; i < c.size(); ++i) {
    double w = p[i] * c[i].coeff * c[i].event_weight;
    out.win_prob += w * (c[i].record.win_prob - b.win_prob);
    out.points_exp += w * (c[i].record.points_exp - b.points_exp);
    for (int h = 0; h < 5; ++h) out.han[h] += w * (c[i].record.han[h] - b.han[h]);
  }
  return out;
}
typedef std::array<std::vector<Candidate>, 38> TileCandidates;
inline TileCandidates baseline_per_tile(const PointsRecord &baseline) {
  TileCandidates out;
  for (int tile = 1; tile < 38; ++tile)
    if (tile % 10) out[tile].push_back(Candidate(baseline, 1, 0));
  return out;
}
}  // namespace simple_dp_sparsemax
