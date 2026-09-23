#include "common/harness_driver.h"
#include "kernel.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

struct ProblemSize {
  int n;
  int mask_density_pct;
};

static bool parse_size(const char* text, ProblemSize* out) {
  char* end = nullptr;
  const long value = std::strtol(text, &end, 10);
  if (!text[0] || !end || value <= 0 || value > 8192 || value % 64) return false;
  long density = 50;
  if (*end) {
    if (end[0] != ':' || end[1] != 'd' || !end[2]) return false;
    char* density_end = nullptr;
    density = std::strtol(end + 2, &density_end, 10);
    if (!density_end || *density_end || density < 0 || density > 100) return false;
  }
  out->n = static_cast<int>(value);
  out->mask_density_pct = static_cast<int>(density);
  return true;
}

static void fill_random(uint64_t* values, std::size_t count, uint64_t seed) {
  std::mt19937_64 rng(seed);
  for (std::size_t i = 0; i < count; ++i) values[i] = rng();
}

static void fill_mask(uint64_t* values, std::size_t count, uint64_t seed,
                      int density_pct) {
  if (density_pct == 0) {
    std::fill(values, values + count, UINT64_C(0));
    return;
  }
  if (density_pct == 100) {
    std::fill(values, values + count, ~UINT64_C(0));
    return;
  }
  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<int> distribution(0, 99);
  for (std::size_t word = 0; word < count; ++word) {
    uint64_t value = 0;
    for (int bit = 0; bit < 64; ++bit) {
      if (distribution(rng) < density_pct) value |= UINT64_C(1) << bit;
    }
    values[word] = value;
  }
}

int main(int argc, char** argv) try {
  if (argc < 3) return 2;
  ProblemSize size{};
  if (!parse_size(argv[2], &size)) return 2;
  const int N = size.n;
  bench::Options options;
  if (!bench::parse_options(argc, argv, 3, &options)) return 2;
  const uint64_t seed = options.seed;

  const std::size_t words = static_cast<std::size_t>(N) * N / 64;
  const std::size_t mask_words = static_cast<std::size_t>(N) / 64;
  std::vector<uint64_t> trusted_matrix(words), trusted_mask(mask_words);

  isolated::SharedArray<uint64_t> matrix(words), mask(mask_words);
  isolated::SharedArray<uint32_t> result(static_cast<std::size_t>(N));
  isolated::Worker worker(
      isolated::worker_executable_path(), isolated::submitted_library_path(),
      "kernel_bitmatrix",
      {matrix.input_region(), mask.input_region(), result.output_region()},
      {std::to_string(N)});

  auto inputs_unchanged = [&]() {
    return isolated::unchanged(matrix.data(), trusted_matrix.data(), matrix.bytes()) &&
           isolated::unchanged(mask.data(), trusted_mask.data(), mask.bytes());
  };
  std::vector<uint32_t> reference(static_cast<std::size_t>(N));
  auto make_case = [&](uint64_t domain) {
    const uint64_t invocation_seed = isolated::trusted_input_seed(seed, domain);
    fill_random(trusted_matrix.data(), words, invocation_seed + 1);
    fill_mask(trusted_mask.data(), mask_words, invocation_seed + 2,
              size.mask_density_pct);
    kernel_bitmatrix_reference(N, trusted_matrix.data(), trusted_mask.data(),
                               reference.data());
    std::memcpy(matrix.data(), trusted_matrix.data(), matrix.bytes());
    std::memcpy(mask.data(), trusted_mask.data(), mask.bytes());
    isolated::poison(result.data(), result.size());
  };
  auto verified = [&](const isolated::Invocation& call) {
    return call.ok && inputs_unchanged() &&
           std::memcmp(result.data(), reference.data(), result.bytes()) == 0;
  };

  auto inspect = [&](const isolated::Invocation& call) {
    harness::report_first_mismatch("result", result.data(), reference.data(), result.size());
    if (!inputs_unchanged()) kv::out("input_integrity", "modified");
    bool passed = call.ok && inputs_unchanged();
    uint32_t max_abs = 0;
    if (passed) {
      for (int i = 0; i < N; ++i) {
        const uint32_t actual = result.data()[i];
        const uint32_t expected = reference[static_cast<std::size_t>(i)];
        const uint32_t error =
            actual > expected ? actual - expected : expected - actual;
        max_abs = std::max(max_abs, error);
        if (error) passed = false;
      }
    }
    return harness::CheckResult{passed, static_cast<double>(max_abs),
                                passed ? 0.0 : 1.0};
  };
  const double bytes = static_cast<double>(N) * N / 8.0;
  return harness::run_mode(
      argv[1], worker, argv[2], options, bytes,
      UINT64_C(0x6269746d61746368), UINT64_C(0x6269746d62656e63),
      UINT64_C(0x6269746d70726f30), UINT64_C(0x6269746d70726f31),
      make_case, inspect, verified);
} catch (const std::exception& error) {
  std::fprintf(stderr, "trusted harness error: %s\n", error.what());
  return 70;
}
