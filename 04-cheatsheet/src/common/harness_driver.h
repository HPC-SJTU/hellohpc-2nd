#pragma once

#include "common/bench.h"
#include "common/isolated_runner.h"
#include "common/kv_out.h"
#include "profiler/hwcounters.h"

#include <cstdint>
#include <cmath>
#include <cstring>

namespace harness {

constexpr int kExecutionError = 70;

struct CheckResult {
  bool passed;
  double max_abs_error;
  double max_rel_error;
};

// Diagnostic only: called from check inspection, never the timed benchmark path.
template <class T>
void report_first_mismatch(const char* field, const T* actual, const T* expected,
                           std::size_t count, double abs_tol = 0, double rel_tol = 0) {
  for (std::size_t i = 0; i < count; ++i) {
    const double a = actual[i], e = expected[i];
    if (!std::isfinite(a) || std::fabs(a-e) > abs_tol + rel_tol*std::fabs(e)) {
      kv::out("mismatch_field", field);
      kv::out("mismatch_flat_index", static_cast<long>(i));
      kv::out("mismatch_expected", e, 12);
      kv::out("mismatch_actual", a, 12);
      return;
    }
  }
}

template <class Prepare, class Inspect>
int run_check(isolated::Worker& worker, const char* size_token, uint64_t domain,
              Prepare&& prepare, Inspect&& inspect) {
  prepare(domain);
  const isolated::Invocation call = worker.invoke();
  if (!call.ok) {
    kv::out("execution_error", call.error.empty() ? "worker failed without diagnostic" : call.error);
    kv::flush();
    return kExecutionError;
  }

  const CheckResult result = inspect(call);
  kv::out("correctness", result.passed ? "passed" : "failed");
  kv::out("max_abs_err", result.max_abs_error);
  kv::out("max_rel_err", result.max_rel_error);
  kv::out("failed_sizes", result.passed ? "none" : size_token);
  kv::flush();
  return 0;
}

template <class Prepare, class Verify>
int run_benchmark(isolated::Worker& worker, const char* size_token,
                  const bench::Options& options, uint64_t domain,
                  double work_per_invocation, Prepare&& prepare,
                  Verify&& verify) {
  const double overhead = isolated::measure_resume_stop_overhead();
  const bench::VerifiedResult measured = bench::run_verified_blocks(
      [&](uint64_t ordinal, double* elapsed_ms) {
        prepare(domain ^ ordinal);
        const isolated::Invocation call = worker.invoke(overhead);
        *elapsed_ms = call.elapsed_ms;
        return verify(call);
      },
      options.samples, options.block_invocations);
  if (!measured.ok) return kExecutionError;

  const bench::Result& result = measured.result;
  const double median = result.median_ms;
  kv::out_fixed("median_time_ms", median, 6);
  kv::out_fixed("block_median_time_ms", result.median_block_ms, 6);
  kv::out("block_invocations", result.block_invocations);
  kv::out("benchmark_samples", result.reps);
  kv::out_fixed("gflops",
                median > 0.0 ? work_per_invocation / 1e6 / median : 0.0, 2);
  kv::out_pct("variance", result.cov_pct / 100.0, 3);
  kv::out("size", size_token);
  kv::flush();
  return 0;
}

template <class Prepare, class Verify>
int run_profile(isolated::Worker& worker, uint64_t warm_domain,
                uint64_t measured_domain, Prepare&& prepare, Verify&& verify) {
  prepare(warm_domain);
  if (!verify(worker.invoke())) return kExecutionError;

  prepare(measured_domain);
  hw::HwCounters counters(worker.pid());
  counters.start();
  const isolated::Invocation measured = worker.invoke();
  counters.stop();
  if (!verify(measured)) return kExecutionError;

  hw::output_profile(counters.read());
  kv::flush();
  return 0;
}

template <class Prepare, class Inspect, class Verify>
int run_mode(const char* mode, isolated::Worker& worker, const char* size_token,
             const bench::Options& options, double work_per_invocation,
             uint64_t check_domain, uint64_t benchmark_domain,
             uint64_t profile_warm_domain, uint64_t profile_measured_domain,
             Prepare&& prepare, Inspect&& inspect, Verify&& verify) {
  if (!std::strcmp(mode, "check"))
    return run_check(worker, size_token, check_domain, prepare, inspect);
  if (!std::strcmp(mode, "bench"))
    return run_benchmark(worker, size_token, options, benchmark_domain,
                         work_per_invocation, prepare, verify);
  if (!std::strcmp(mode, "profile"))
    return run_profile(worker, profile_warm_domain, profile_measured_domain,
                       prepare, verify);
  return 2;
}

}  // namespace harness
