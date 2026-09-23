// Trusted parent-side perf_event_open counters for an already-stopped worker.
//
// The submitted shared object is never linked into this process. The caller
// creates an isolated::Worker first, then binds counters to every existing
// worker thread while the leader is at its READY/SIGSTOP boundary. Events are
// disabled until start() and no counter descriptor is passed to the worker.
#pragma once

#include <cstdint>
#include <sys/types.h>

namespace hw {

struct RawCount {
  uint64_t value = 0;
  bool valid = false;
};

struct Counters {
  RawCount instructions;
  RawCount cycles;
  RawCount branches;
  RawCount branch_miss;
  RawCount l1d_miss;
  RawCount llc_miss;

  double ipc() const {
    if (!instructions.valid || !cycles.valid || cycles.value == 0) return -1.0;
    return static_cast<double>(instructions.value) /
           static_cast<double>(cycles.value);
  }
  double l1d_mpki() const {
    if (!l1d_miss.valid || !instructions.valid || instructions.value == 0)
      return -1.0;
    return static_cast<double>(l1d_miss.value) /
           static_cast<double>(instructions.value) * 1000.0;
  }
  double llc_mpki() const {
    if (!llc_miss.valid || !instructions.valid || instructions.value == 0)
      return -1.0;
    return static_cast<double>(llc_miss.value) /
           static_cast<double>(instructions.value) * 1000.0;
  }
  double branch_miss_rate() const {
    if (!branch_miss.valid || !branches.valid || branches.value == 0)
      return -1.0;
    return static_cast<double>(branch_miss.value) /
           static_cast<double>(branches.value);
  }

  bool usable() const { return ipc() >= 0.0; }
};

class HwCounters {
 public:
  // target_pid must be the stopped worker's leader PID. Permission and PMU
  // failures degrade to unavailable events.
  explicit HwCounters(pid_t target_pid);
  ~HwCounters();
  HwCounters(const HwCounters&) = delete;
  HwCounters& operator=(const HwCounters&) = delete;

  void start();
  void stop();
  Counters read() const;

 private:
  struct Impl;
  Impl* impl_;
};

// Emit the stable agent-facing profile schema. A missing core event produces
// profile_status: unavailable and all metrics n/a; unsupported optional events
// only make their own derived metric n/a.
void output_profile(const Counters& counters);

}  // namespace hw
