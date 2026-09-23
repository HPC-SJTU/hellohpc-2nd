#include <cassert>
#include <cmath>

#include "../ai_src/simple_dp_sparsemax.hpp"
#include "../ai_src/sparsemax_bellman.hpp"

namespace {
bool near(const double a, const double b, const double tolerance = 1e-12) {
  return std::fabs(a - b) < tolerance;
}

void test_sparsemax_bellman() {
  using namespace sparsemax_bellman;
  const Record a{.1, 100, .2, 100}, b{.9, 900, .8, 900};
  std::vector<Candidate> c{{a, 0}, {b, 2}};
  const std::vector<double> p = weights(c);
  assert(near(p[0] + p[1], 1));
  const Record r = mix_all(c);
  assert(near(r.points_exp, p[0] * 100 + p[1] * 900));
  assert(near(r.win_prob, p[0] * .1 + p[1] * .9));

  const Record tie_a{.2, 20, .3, 500}, tie_b{.8, 80, .7, 500};
  const std::vector<Candidate> ties{{tie_a}, {tie_b}};
  const Record tie_result = mix_all(ties);
  assert(near(tie_result.win_prob, .5));

  // Coefficients stay attached to complete records.  mix_delta applies each
  // action coefficient after projection instead of averaging discrete flags.
  const Record baseline{.2, 200, .3, 200};
  const std::vector<Candidate> affine{{baseline, 1, 0}, {{.6, 600, .7, 600}, .25, .1}};
  const std::vector<double> affine_p = weights(affine);
  const Record delta = mix_delta(affine, baseline);
  assert(near(delta.points_exp, affine_p[1] * .25 * .1 * 400));
  assert(near(delta.win_prob, affine_p[1] * .25 * .1 * .4));

  // Large common score offsets do not affect weights or mixed output deltas.
  if (kTauPoints > 0.0) {
    const double gap = .25 * kTauPoints;
    const std::vector<Candidate> unshifted{{{.1, 10, .2, 100}, 1}, {{.9, 90, .8, 100 + gap}, 1}};
    const std::vector<Candidate> shifted{{{.1, 10, .2, 1e12 + 100}, 1},
                                         {{.9, 90, .8, 1e12 + 100 + gap}, 1}};
    const std::vector<double> unshifted_p = weights(unshifted);
    const std::vector<double> shifted_p = weights(shifted);
    assert(near(shifted_p[0] + shifted_p[1], 1));
    assert(near(shifted_p[0], unshifted_p[0]));
    assert(near(shifted_p[1], unshifted_p[1]));
    assert(near(mix_all(shifted).win_prob, mix_all(unshifted).win_prob));
    assert(near(mix_all(shifted).win_exp, mix_all(unshifted).win_exp));

    // Independent per-tile simplexes retain the same tile-2 result even if
    // another tile has an overwhelmingly better candidate.
    const std::vector<Candidate> tile1{{baseline, 1, 0}, {{0, 0, 0, 900}, 1, .2}};
    const std::vector<Candidate> tile2{{baseline, 1, 0}, {{0, 0, 0, 200 + gap}, 1, .3}};
    const Record tile2_delta = mix_delta(tile2, baseline);
    const std::vector<Candidate> cross_tile_wrong{
        {baseline, 1, 0}, {{0, 0, 0, 900}, 1, .2}, {{0, 0, 0, 200 + gap}, 1, .3}};
    assert(tile2_delta.points_exp > 0.0);
    assert(near(mix_delta(tile2, baseline).points_exp, tile2_delta.points_exp));
    assert(!near(mix_delta(cross_tile_wrong, baseline).points_exp,
                 mix_delta(tile1, baseline).points_exp + tile2_delta.points_exp));

    // Positive tau projects both legal fold choices without a hard score
    // prefilter, so the slightly losing fold still has nonzero weight.
    const Record play{1, 100, 1, 500}, fold{0, 0, 0, 500 - .25 * kTauPoints};
    const std::vector<double> fold_p = weights({{play}, {fold}});
    assert(fold_p[0] > 0.0 && fold_p[1] > 0.0);
  }
}

void test_simple_dp_sparsemax() {
  using namespace simple_dp_sparsemax;
  const PointsRecord a{.2, 10, {{1, 2, 3, 4, 5}}}, b{.8, 10, {{5, 4, 3, 2, 1}}};
  const double gap = .25;
  std::vector<Candidate> candidates{Candidate(a), Candidate({.8, 10 + gap, {{5, 4, 3, 2, 1}}})};
  std::vector<Candidate> shifted{Candidate({.2, 1e9 + 10, {{1, 2, 3, 4, 5}}}),
                                 Candidate({.8, 1e9 + 10 + gap, {{5, 4, 3, 2, 1}}})};
  const std::vector<double> probabilities = weights(candidates);
  const std::vector<double> shifted_probabilities = weights(shifted);
  assert(near(probabilities[0], shifted_probabilities[0], 1e-10) &&
         near(probabilities[1], shifted_probabilities[1], 1e-10));
  const PointsRecord result = mix(candidates);
  assert(near(result.win_prob, probabilities[0] * .2 + probabilities[1] * .8, 1e-10));
  for (int han = 0; han < 5; ++han)
    assert(near(result.han[han], probabilities[0] * a.han[han] + probabilities[1] * b.han[han],
                1e-10));

  TileCandidates tiles = baseline_per_tile(a);
  assert(tiles[1].size() == 1 && tiles[10].empty());
  tiles[1].push_back(Candidate({.3, 9.9, {{0, 0, 0, 0, 0}}}));
  tiles[1].push_back(Candidate({.4, 10.1, {{0, 0, 0, 0, 0}}}));
  assert(tiles[1].size() == 3);

  std::vector<Candidate> local{Candidate(a, 1, 0),
                               Candidate({.6, 10 + gap, {{2, 3, 4, 5, 6}}}, 3, .25),
                               Candidate({.1, 10 - gap, {{0, 1, 2, 3, 4}}}, 1, .5)};
  const std::vector<double> local_probabilities = weights(local);
  const PointsRecord delta = mix_delta(local, a);
  assert(near(delta.points_exp,
              local_probabilities[1] * 3 * .25 * gap + local_probabilities[2] * .5 * (-gap),
              1e-10));
  assert(near(delta.han[2],
              local_probabilities[1] * 3 * .25 * (4 - 3) + local_probabilities[2] * .5 * (2 - 3),
              1e-10));
  std::vector<Candidate> losing{Candidate(a, 1, 0),
                                Candidate({.1, 10 - gap, {{0, 0, 0, 0, 0}}}, 1, 1)};
  assert(mix_delta(losing, a).points_exp < 0);

  TileCandidates independent = baseline_per_tile(a);
  independent[1].push_back(Candidate({1, 110, {{0, 0, 0, 0, 0}}}, 1, .2));
  independent[2].push_back(Candidate({1, 10 + gap, {{0, 0, 0, 0, 0}}}, 1, .3));
  const PointsRecord tile2 = mix_delta(independent[2], a);
  assert(tile2.points_exp > 0);
  std::vector<Candidate> wrong = independent[1];
  wrong.push_back(independent[2][1]);
  assert(!near(mix_delta(wrong, a).points_exp,
               mix_delta(independent[1], a).points_exp + tile2.points_exp, 1e-10));
}

}  // namespace

int main() {
  test_sparsemax_bellman();
  test_simple_dp_sparsemax();
  return 0;
}
