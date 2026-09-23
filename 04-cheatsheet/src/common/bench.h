#pragma once
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace bench {

struct Result {
  double median_ms = 0.0;
  double cov_pct = 0.0;
  int reps = 0;
  int block_invocations = 1;
  double median_block_ms = 0.0;
};

// Formal measurements use an odd number of statistical samples.  Each sample
// is a block of independently generated and verified kernel invocations.  The
// trusted caller chooses block_invocations; submitted code cannot shorten the
// window by influencing this helper.
constexpr int kDefaultSamples = 11;
constexpr int kPilotSamples = 3;
constexpr int kWarmupInvocations = 3;
constexpr int kMaxBlockInvocations = 4096;
constexpr int kMaxSamples = 101;

inline bool valid_block_shape(int samples, int block_invocations);

struct Options {
  int samples = kDefaultSamples;
  int block_invocations = 1;
  std::uint64_t seed = 0;
};

inline bool parse_bounded_positive_int(const char* text, int maximum, int* output) {
  if (!text || !text[0] || text[0] < '0' || text[0] > '9') return false;
  errno = 0;
  char* end = nullptr;
  const long value = std::strtol(text, &end, 10);
  if (errno == ERANGE || !end || *end || value <= 0 || value > maximum)
    return false;
  *output = static_cast<int>(value);
  return true;
}

inline bool parse_seed(const char* text, std::uint64_t* output) {
  if (!text || !text[0] || text[0] < '0' || text[0] > '9') return false;
  errno = 0;
  char* end = nullptr;
  const unsigned long long value = std::strtoull(text, &end, 10);
  if (errno == ERANGE || !end || *end) return false;
  *output = static_cast<std::uint64_t>(value);
  return true;
}

// Parse only the trusted harness flags.  Unknown, repeated, truncated and
// out-of-range options fail closed instead of silently changing a measurement.
inline bool parse_options(int argc, char** argv, int first, Options* output) {
  if (!output || first < 0 || first > argc) return false;
  Options parsed;
  bool saw_samples = false;
  bool saw_block = false;
  bool saw_seed = false;
  for (int index = first; index < argc; ++index) {
    const char* option = argv[index];
    if (!std::strcmp(option, "--reps")) {
      if (saw_samples || ++index >= argc ||
          !parse_bounded_positive_int(argv[index], kMaxSamples,
                                      &parsed.samples))
        return false;
      saw_samples = true;
    } else if (!std::strcmp(option, "--block-invocations")) {
      if (saw_block || ++index >= argc ||
          !parse_bounded_positive_int(argv[index], kMaxBlockInvocations,
                                      &parsed.block_invocations))
        return false;
      saw_block = true;
    } else if (!std::strcmp(option, "--seed")) {
      if (saw_seed || ++index >= argc || !parse_seed(argv[index], &parsed.seed))
        return false;
      saw_seed = true;
    } else {
      return false;
    }
  }
  if (!valid_block_shape(parsed.samples, parsed.block_invocations)) return false;
  *output = parsed;
  return true;
}

inline bool valid_block_shape(int samples, int block_invocations) {
  return samples > 0 && samples <= kMaxSamples &&
         (samples % 2) == 1 &&
         block_invocations > 0 &&
         block_invocations <= kMaxBlockInvocations;
}

struct VerifiedResult {
  bool ok = false;
  Result result;
};

// invoke_once receives a never-repeated ordinal and stores trusted elapsed
// kernel milliseconds in *elapsed_ms.  It must prepare a fresh keyed input,
// invoke the isolated worker and verify both inputs and output before returning
// true.  Input generation/reference/checking remain outside the timed window;
// every kernel invocation in the accumulated window is nevertheless checked.
template <typename InvokeOnce>
inline VerifiedResult run_verified_blocks(InvokeOnce&& invoke_once,
                                          int samples,
                                          int block_invocations,
                                          int warmup = kWarmupInvocations) {
  VerifiedResult output;
  if (!valid_block_shape(samples, block_invocations) || warmup < 0) return output;

  std::uint64_t ordinal = 0;
  for (int index = 0; index < warmup; ++index) {
    double elapsed_ms = 0.0;
    if (!invoke_once(ordinal++, &elapsed_ms) || !std::isfinite(elapsed_ms) ||
        elapsed_ms < 0.0) return output;
  }

  std::vector<double> per_invocation_ms;
  per_invocation_ms.reserve(static_cast<std::size_t>(samples));
  for (int sample = 0; sample < samples; ++sample) {
    double block_ms = 0.0;
    for (int invocation = 0; invocation < block_invocations; ++invocation) {
      double elapsed_ms = 0.0;
      if (!invoke_once(ordinal++, &elapsed_ms) || !std::isfinite(elapsed_ms) ||
          elapsed_ms < 0.0) return output;
      block_ms += elapsed_ms;
    }
    per_invocation_ms.push_back(
        block_ms / static_cast<double>(block_invocations));
  }

  std::vector<double> sorted = per_invocation_ms;
  std::sort(sorted.begin(), sorted.end());
  const std::size_t middle = sorted.size() / 2;
  Result result;
  result.reps = samples;
  result.block_invocations = block_invocations;
  result.median_ms = sorted[middle];
  result.median_block_ms =
      result.median_ms * static_cast<double>(block_invocations);
  double mean_ms = 0.0;
  for (double value : per_invocation_ms) mean_ms += value;
  mean_ms /= static_cast<double>(per_invocation_ms.size());
  double variance = 0.0;
  for (double value : per_invocation_ms)
    variance += (value - mean_ms) * (value - mean_ms);
  variance /= static_cast<double>(per_invocation_ms.size());
  result.cov_pct =
      mean_ms > 0.0 ? std::sqrt(variance) / mean_ms * 100.0 : 0.0;
  output.ok = true;
  output.result = result;
  return output;
}

}  // namespace bench
