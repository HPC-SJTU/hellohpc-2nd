// Parent-side Linux perf counters. Each event is independent so an optional
// unsupported PMU event does not make IPC unavailable. enabled/running scaling
// accounts for multiplexing.
#include "hwcounters.h"

#include "common/kv_out.h"

#include <dirent.h>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace hw {
namespace {

long open_perf_event(struct perf_event_attr* attr, pid_t pid, int cpu,
                     int group_fd, unsigned long flags) {
  return syscall(SYS_perf_event_open, attr, pid, cpu, group_fd, flags);
}

uint64_t cache_config(uint64_t id, uint64_t op, uint64_t result) {
  return id | (op << 8) | (result << 16);
}

struct ReadFormat {
  uint64_t value;
  uint64_t time_enabled;
  uint64_t time_running;
};

enum EventIndex {
  kInstructions = 0,
  kCycles,
  kBranches,
  kBranchMisses,
  kL1dMisses,
  kLlcMisses,
  kEventCount,
};

std::vector<pid_t> thread_ids(pid_t target_pid) {
  std::vector<pid_t> result;
  if (target_pid <= 0) return result;

  char path[64];
  std::snprintf(path, sizeof(path), "/proc/%d/task", target_pid);
  DIR* directory = opendir(path);
  if (!directory) return result;

  while (dirent* entry = readdir(directory)) {
    char* end = nullptr;
    const long value = std::strtol(entry->d_name, &end, 10);
    if (*entry->d_name != '\0' && end && *end == '\0' && value > 0 &&
        value <= std::numeric_limits<pid_t>::max()) {
      result.push_back(static_cast<pid_t>(value));
    }
  }
  closedir(directory);
  return result;
}

void open_one(int* fd, pid_t target_pid, uint32_t type, uint64_t config) {
  *fd = -1;
  if (target_pid <= 0) return;
  struct perf_event_attr attr;
  std::memset(&attr, 0, sizeof(attr));
  attr.type = type;
  attr.size = sizeof(attr);
  attr.config = config;
  attr.disabled = 1;
  attr.exclude_kernel = 1;
  attr.exclude_hv = 1;
  attr.inherit = 0;
  attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED |
                     PERF_FORMAT_TOTAL_TIME_RUNNING;
  const long result = open_perf_event(
      &attr, target_pid, -1, -1, PERF_FLAG_FD_CLOEXEC);
  if (result >= 0) *fd = static_cast<int>(result);
}

RawCount read_scaled(int fd) {
  RawCount count;
  if (fd < 0) return count;
  ReadFormat value{};
  ssize_t bytes;
  do {
    bytes = ::read(fd, &value, sizeof(value));
  } while (bytes < 0 && errno == EINTR);
  if (bytes != static_cast<ssize_t>(sizeof(value)) || value.time_running == 0)
    return count;
  const double scaled = value.time_enabled == value.time_running
      ? static_cast<double>(value.value)
      : static_cast<double>(value.value) *
            static_cast<double>(value.time_enabled) /
            static_cast<double>(value.time_running);
  count.value = static_cast<uint64_t>(scaled + 0.5);
  count.valid = true;
  return count;
}

using ThreadEvents = std::array<int, kEventCount>;

RawCount read_sum(const std::vector<ThreadEvents>& threads, EventIndex event) {
  RawCount total;
  if (threads.empty()) return total;
  for (const ThreadEvents& descriptors : threads) {
    const RawCount current = read_scaled(descriptors[event]);
    if (!current.valid ||
        current.value > std::numeric_limits<uint64_t>::max() - total.value) {
      return {};
    }
    total.value += current.value;
  }
  total.valid = true;
  return total;
}

void output_metric(const char* key, double value, bool percent = false) {
  if (value < 0.0) {
    kv::out_na(key);
  } else if (percent) {
    kv::out_pct(key, value, 3);
  } else {
    kv::out(key, value, 6);
  }
}

}  // namespace

struct HwCounters::Impl {
  std::vector<ThreadEvents> threads;
};

HwCounters::HwCounters(pid_t target_pid) : impl_(new Impl()) {
  for (pid_t tid : thread_ids(target_pid)) {
    impl_->threads.emplace_back();
    ThreadEvents& fd = impl_->threads.back();
    fd.fill(-1);
    open_one(&fd[kInstructions], tid, PERF_TYPE_HARDWARE,
             PERF_COUNT_HW_INSTRUCTIONS);
    open_one(&fd[kCycles], tid, PERF_TYPE_HARDWARE,
             PERF_COUNT_HW_CPU_CYCLES);
    open_one(&fd[kBranches], tid, PERF_TYPE_HARDWARE,
             PERF_COUNT_HW_BRANCH_INSTRUCTIONS);
    open_one(&fd[kBranchMisses], tid, PERF_TYPE_HARDWARE,
             PERF_COUNT_HW_BRANCH_MISSES);
    open_one(&fd[kL1dMisses], tid, PERF_TYPE_HW_CACHE,
             cache_config(PERF_COUNT_HW_CACHE_L1D,
                          PERF_COUNT_HW_CACHE_OP_READ,
                          PERF_COUNT_HW_CACHE_RESULT_MISS));
    open_one(&fd[kLlcMisses], tid, PERF_TYPE_HW_CACHE,
             cache_config(PERF_COUNT_HW_CACHE_LL,
                          PERF_COUNT_HW_CACHE_OP_READ,
                          PERF_COUNT_HW_CACHE_RESULT_MISS));
  }
}

HwCounters::~HwCounters() {
  for (const ThreadEvents& fd : impl_->threads) {
    for (int descriptor : fd) {
      if (descriptor >= 0) close(descriptor);
    }
  }
  delete impl_;
}

void HwCounters::start() {
  for (ThreadEvents& fd : impl_->threads) {
    for (int& descriptor : fd) {
      if (descriptor < 0) continue;
      if (ioctl(descriptor, PERF_EVENT_IOC_RESET, 0) != 0 ||
          ioctl(descriptor, PERF_EVENT_IOC_ENABLE, 0) != 0) {
        close(descriptor);
        descriptor = -1;
      }
    }
  }
}

void HwCounters::stop() {
  for (ThreadEvents& fd : impl_->threads) {
    for (int& descriptor : fd) {
      if (descriptor < 0) continue;
      if (ioctl(descriptor, PERF_EVENT_IOC_DISABLE, 0) != 0) {
        close(descriptor);
        descriptor = -1;
      }
    }
  }
}

Counters HwCounters::read() const {
  Counters counters;
  counters.instructions = read_sum(impl_->threads, kInstructions);
  counters.cycles = read_sum(impl_->threads, kCycles);
  counters.branches = read_sum(impl_->threads, kBranches);
  counters.branch_miss = read_sum(impl_->threads, kBranchMisses);
  counters.l1d_miss = read_sum(impl_->threads, kL1dMisses);
  counters.llc_miss = read_sum(impl_->threads, kLlcMisses);
  return counters;
}

void output_profile(const Counters& counters) {
  kv::out("profile_status", counters.usable() ? "available" : "unavailable");
  if (!counters.usable()) {
    kv::out_na("ipc");
    kv::out_na("l1d_mpki");
    kv::out_na("llc_mpki");
    kv::out_na("branch_miss_rate");
    return;
  }
  output_metric("ipc", counters.ipc());
  output_metric("l1d_mpki", counters.l1d_mpki());
  output_metric("llc_mpki", counters.llc_mpki());
  output_metric("branch_miss_rate", counters.branch_miss_rate(), true);
}

}  // namespace hw
