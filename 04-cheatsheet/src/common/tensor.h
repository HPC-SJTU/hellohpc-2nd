#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>

namespace tn {

inline void fill_uniform(float* p, std::size_t n, uint64_t seed, float lo = -1.0f, float hi = 1.0f) {
  std::mt19937_64 rng(seed);
  std::uniform_real_distribution<float> dist(lo, hi);
  for (std::size_t i = 0; i < n; ++i) p[i] = dist(rng);
}

struct CompareResult {
  bool passed = true;
  double max_abs_err = 0.0;
  double max_rel_err = 0.0;
};

inline CompareResult compare(const float* got, const float* ref, std::size_t n,
                             double eps_abs, double eps_rel) {
  CompareResult r;
  for (std::size_t i = 0; i < n; ++i) {
    float g = got[i], y = ref[i];
    if (std::isnan(g) || std::isinf(g)) {
      r.passed = false;
      return r;
    }
    double abs_err = std::fabs((double)g - (double)y);
    double denom = std::fabs((double)y);
    double rel_err = denom > 0 ? abs_err / denom : abs_err;
    if (abs_err > r.max_abs_err) r.max_abs_err = abs_err;
    if (rel_err > r.max_rel_err) r.max_rel_err = rel_err;
    if (abs_err > eps_abs + eps_rel * denom) r.passed = false;
  }
  return r;
}

}  // namespace tn
