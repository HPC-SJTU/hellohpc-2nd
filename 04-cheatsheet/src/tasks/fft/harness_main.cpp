#include "common/harness_driver.h"
#include "common/tensor.h"
#include "kernel.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

struct Size { int batch, length; };
static constexpr double kAbs = 1e-2, kRel = 1e-3;

static bool parse_size(const char* text, Size* out) {
  char tail = 0;
  const bool parsed = std::sscanf(text, "%dx%d%c", &out->batch, &out->length,
                                  &tail) == 2;
  return parsed && out->batch > 0 && out->length >= 16 &&
         out->length <= 8192 && (out->length & (out->length - 1)) == 0;
}

int main(int argc, char** argv) try {
  if (argc < 3) return 2;
  Size size{};
  if (!parse_size(argv[2], &size)) return 2;
  bench::Options options;
  if (!bench::parse_options(argc, argv, 3, &options)) return 2;
  const uint64_t seed = options.seed;

  const std::size_t count = static_cast<std::size_t>(2) * size.batch * size.length;
  std::vector<float> trusted_input(count), reference(count);
  isolated::SharedArray<float> input(count), output(count);
  isolated::Worker worker(
      isolated::worker_executable_path(), isolated::submitted_library_path(),
      "kernel_fft", {input.input_region(), output.output_region()},
      {std::to_string(size.batch), std::to_string(size.length)});

  auto make_case = [&](uint64_t domain) {
    const uint64_t invocation_seed = isolated::trusted_input_seed(seed, domain);
    tn::fill_uniform(trusted_input.data(), count, invocation_seed, -1.0f, 1.0f);
    kernel_fft_reference(trusted_input.data(), reference.data(),
                         size.batch, size.length);
    std::memcpy(input.data(), trusted_input.data(), input.bytes());
    isolated::poison(output.data(), output.size());
  };
  auto verified = [&](const isolated::Invocation& call) {
    return call.ok && isolated::unchanged(input.data(), trusted_input.data(), input.bytes()) &&
           tn::compare(output.data(), reference.data(), count, kAbs, kRel).passed;
  };

  auto inspect = [&](const isolated::Invocation& call) {
    harness::report_first_mismatch("output_float", output.data(), reference.data(), count, kAbs, kRel);
    if (!isolated::unchanged(input.data(), trusted_input.data(), input.bytes())) kv::out("input_integrity", "modified");
    const tn::CompareResult comparison =
        tn::compare(output.data(), reference.data(), count, kAbs, kRel);
    return harness::CheckResult{
        call.ok && isolated::unchanged(input.data(), trusted_input.data(),
                                       input.bytes()) &&
            comparison.passed,
        comparison.max_abs_err, comparison.max_rel_err};
  };
  const double ops = 5.0 * size.batch * size.length * std::log2(size.length);
  return harness::run_mode(
      argv[1], worker, argv[2], options, ops,
      UINT64_C(0x666674636865636b), UINT64_C(0x66667462656e6368),
      UINT64_C(0x66667470726f6630), UINT64_C(0x66667470726f6631),
      make_case, inspect, verified);
} catch (const std::exception& error) {
  std::fprintf(stderr, "trusted harness error: %s\n", error.what());
  return 70;
}
