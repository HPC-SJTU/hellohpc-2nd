#include "interactor.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

auto fail(std::string_view message) -> void {
  static_cast<void>(::write(STDERR_FILENO, message.data(), message.size()));
  static_cast<void>(::write(STDERR_FILENO, "\n", 1));
  std::exit(1);
}

auto expect(bool condition, std::string_view message) -> void {
  if (!condition) fail(message);
}

class Descriptor {
 public:
  explicit Descriptor(int value = -1) : value_(value) {}
  Descriptor(const Descriptor &) = delete;
  auto operator=(const Descriptor &) -> Descriptor & = delete;
  Descriptor(Descriptor &&other) noexcept : value_(std::exchange(other.value_, -1)) {}
  auto operator=(Descriptor &&other) noexcept -> Descriptor & {
    if (this != &other) {
      if (value_ >= 0) ::close(value_);
      value_ = std::exchange(other.value_, -1);
    }
    return *this;
  }
  ~Descriptor() {
    if (value_ >= 0) ::close(value_);
  }
  auto get() const -> int { return value_; }
  auto reset() -> void {
    if (value_ >= 0) ::close(std::exchange(value_, -1));
  }

 private:
  int value_;
};

auto record_fd(std::string_view text) -> Descriptor {
  Descriptor descriptor(::memfd_create("interactor-test", MFD_CLOEXEC));
  if (descriptor.get() < 0) fail("cannot create record fd");
  std::string_view remaining = text;
  while (!remaining.empty()) {
    const ssize_t count = ::write(descriptor.get(), remaining.data(), remaining.size());
    if (count <= 0) fail("cannot write record fd");
    remaining.remove_prefix(static_cast<std::size_t>(count));
  }
  if (::lseek(descriptor.get(), 0, SEEK_SET) < 0) fail("cannot rewind record fd");
  return descriptor;
}

auto read_events(std::string_view text) -> std::vector<full_analyze_interactor::Event> {
  Descriptor input = record_fd(text);
  full_analyze_interactor::EventReader reader(input.get());
  std::vector<full_analyze_interactor::Event> events;
  while (true) {
    auto result = reader.next();
    if (!result) fail(result.error());
    if (!*result) return events;
    events.push_back(std::move(**result));
  }
}

auto run_child(std::string_view fixture, std::string_view mode, std::string_view record,
               int mask = 15) -> int {
  Descriptor input = record_fd(record);
  const std::vector<std::string_view> command{fixture, mode};
  Descriptor null_fd(::open("/dev/null", O_WRONLY | O_CLOEXEC));
  if (null_fd.get() < 0) fail("cannot open /dev/null");
  return full_analyze_interactor::run(
      input.get(), mask, command,
      full_analyze_interactor::RunOptions{null_fd.get(), null_fd.get()});
}

struct CapturedRun {
  int status;
  std::string error;
};

auto run_captured(std::string_view fixture, std::string_view mode, std::string_view record)
    -> CapturedRun {
  std::array<int, 2> error_pipe{};
  if (::pipe2(error_pipe.data(), O_CLOEXEC) != 0) fail("cannot create capture pipe");
  Descriptor error_read(error_pipe[0]);
  Descriptor error_write(error_pipe[1]);
  Descriptor input = record_fd(record);
  Descriptor null_fd(::open("/dev/null", O_WRONLY | O_CLOEXEC));
  if (null_fd.get() < 0) fail("cannot open /dev/null");
  const std::vector<std::string_view> command{fixture, mode};
  const int status = full_analyze_interactor::run(
      input.get(), 15, command,
      full_analyze_interactor::RunOptions{null_fd.get(), error_write.get()});
  error_write.reset();
  std::string error;
  std::array<char, 4096> bytes{};
  while (true) {
    const ssize_t count = ::read(error_read.get(), bytes.data(), bytes.size());
    if (count > 0) {
      error.append(bytes.data(), static_cast<std::size_t>(count));
    } else if (count < 0 && errno == EINTR) {
      continue;
    } else {
      break;
    }
  }
  return CapturedRun{status, std::move(error)};
}

auto timed_run(std::string_view fixture, std::string_view mode, std::string_view record,
               std::chrono::milliseconds maximum) -> int {
  const auto start = std::chrono::steady_clock::now();
  const int status = run_child(fixture, mode, record);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  expect(elapsed < maximum, "interactor did not terminate within its deadline");
  return status;
}

auto run_backpressured(std::string_view fixture) -> int {
  std::array<int, 2> output_pipe{};
  std::array<int, 2> error_pipe{};
  if (::pipe2(output_pipe.data(), O_CLOEXEC) != 0 || ::pipe2(error_pipe.data(), O_CLOEXEC) != 0) {
    fail("cannot create destination pipes");
  }
  Descriptor output_read(output_pipe[0]);
  Descriptor output_write(output_pipe[1]);
  Descriptor error_read(error_pipe[0]);
  Descriptor error_write(error_pipe[1]);
  const int output_flags = ::fcntl(output_write.get(), F_GETFL);
  const int error_flags = ::fcntl(error_write.get(), F_GETFL);
  const auto drain = [](int descriptor) -> void {
    std::array<char, 4096> bytes{};
    while (true) {
      const ssize_t count = ::read(descriptor, bytes.data(), bytes.size());
      if (count > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(20));
      } else if (count < 0 && errno == EINTR) {
        continue;
      } else {
        return;
      }
    }
  };
  std::thread output_drain(drain, output_read.get());
  std::thread error_drain(drain, error_read.get());
  Descriptor input = record_fd("GAME\t0\tx\n");
  const std::vector<std::string_view> command{fixture, "large"};
  const int status = full_analyze_interactor::run(
      input.get(), 15, command,
      full_analyze_interactor::RunOptions{output_write.get(), error_write.get()});
  expect(::fcntl(output_write.get(), F_GETFL) == output_flags,
         "stdout destination flags were changed");
  expect(::fcntl(error_write.get(), F_GETFL) == error_flags,
         "stderr destination flags were changed");
  output_write.reset();
  error_write.reset();
  output_drain.join();
  error_drain.join();
  return status;
}

auto run_without_reader(std::string_view fixture, std::string_view mode) -> int {
  std::array<int, 2> output_pipe{};
  std::array<int, 2> error_pipe{};
  if (::pipe2(output_pipe.data(), O_CLOEXEC) != 0 || ::pipe2(error_pipe.data(), O_CLOEXEC) != 0) {
    fail("cannot create undrained destination pipes");
  }
  Descriptor output_read(output_pipe[0]);
  Descriptor output_write(output_pipe[1]);
  Descriptor error_read(error_pipe[0]);
  Descriptor error_write(error_pipe[1]);
  Descriptor input = record_fd("GAME\t0\tx\n");
  const std::vector<std::string_view> command{fixture, mode};
  const auto start = std::chrono::steady_clock::now();
  const int status = full_analyze_interactor::run(
      input.get(), 15, command,
      full_analyze_interactor::RunOptions{output_write.get(), error_write.get()});
  expect(std::chrono::steady_clock::now() - start < std::chrono::seconds(2),
         "undrained destination stalled interactor");
  return status;
}

auto run_with_delayed_reader(std::string_view fixture) -> std::pair<int, std::size_t> {
  std::array<int, 2> output_pipe{};
  if (::pipe2(output_pipe.data(), O_CLOEXEC) != 0) fail("cannot create delayed output pipe");
  Descriptor output_read(output_pipe[0]);
  Descriptor output_write(output_pipe[1]);
  Descriptor null_fd(::open("/dev/null", O_WRONLY | O_CLOEXEC));
  Descriptor input = record_fd("GAME\t0\tx\n");
  std::size_t received = 0;
  std::thread reader([descriptor = output_read.get(), &received]() -> void {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::array<char, 8192> bytes{};
    while (received != 256U * 1024U) {
      const ssize_t count = ::read(descriptor, bytes.data(), bytes.size());
      if (count > 0) {
        received += static_cast<std::size_t>(count);
      } else if (count < 0 && errno == EINTR) {
        continue;
      } else {
        return;
      }
    }
  });
  const std::vector<std::string_view> command{fixture, "output_256k"};
  const int status = full_analyze_interactor::run(
      input.get(), 15, command,
      full_analyze_interactor::RunOptions{output_write.get(), null_fd.get()});
  output_write.reset();
  reader.join();
  return {status, received};
}

constexpr std::string_view RECORD =
    "GAME\t0\tx\n"
    "ROUND\t1\t1m\n"
    "HAND\t1\t0\tx\nHAND\t1\t1\tx\nHAND\t1\t2\tx\nHAND\t1\t3\tx\n"
    "ACTION\t2\tDRAW\t0\t1m\n"
    "ACTION\t3\tWIN\t0\t1\t1m\n"
    "WIN_RESULT\t3\tx\n"
    "DRAWN_EXHAUSTIVE\t4\tx\n"
    "HAND\t4\t0\tx\n"
    "HAND\t4\t1\tx\n"
    "HAND\t4\t2\tx\n"
    "HAND\t4\t3\tx\n";
constexpr std::string_view DRAW = "ACTION\t0\tDRAW\t0\t1m\n";
constexpr std::string_view DISCARD = "ACTION\t0\tDISCARD\t0\t1m\n";

}  // namespace

auto main(int argc, char *argv[]) -> int {
  if (argc != 2) fail("fixture path is required");
  const std::string_view fixture(argv[1]);

  const auto events = read_events(RECORD);
  expect(events.size() == 5, "event framing count is wrong");
  expect(events[1].text ==
             "ROUND\t1\t1m\nHAND\t1\t0\tx\nHAND\t1\t1\tx\n"
             "HAND\t1\t2\tx\nHAND\t1\t3\tx\n",
         "ROUND framing is wrong");
  expect(events[3].text.find("WIN_RESULT\t3") != std::string::npos, "WIN framing is wrong");
  expect(events[4].text.find("HAND\t4\t3") != std::string::npos, "drawn framing is wrong");

  expect(run_child(fixture, "sync", RECORD) == 0,
         "next event was sent before all FULL_ANALYZE_STEP records");
  expect(run_child(fixture, "zero_step", "GAME\t0\tx\nGAME\t1\tx\n") == 0,
         "zero-step event did not advance");
  expect(timed_run(fixture, "normal", "GAME\t0\tx\n", std::chrono::milliseconds(500)) == 0,
         "normal child did not finish promptly");
  for (int mask = 1; mask <= 15; ++mask) {
    const std::string mode = "mask" + std::to_string(mask);
    expect(run_child(fixture, mode, DRAW, mask) == 0, "mask synchronization failed");
    expect(run_child(fixture, mode, DISCARD, mask) == 0, "discard mask synchronization failed");
  }

  expect(run_child(fixture, "large", "GAME\t0\tx\n") == 0,
         "large stdout/stderr transfer deadlocked or failed");
  expect(run_backpressured(fixture) == 0, "destination backpressure deadlocked child pipes");
  expect(run_without_reader(fixture, "output_256k") == 2,
         "undeliverable child output was silently accepted");
  const auto [delayed_status, delayed_bytes] = run_with_delayed_reader(fixture);
  expect(delayed_status == 0 && delayed_bytes == 256U * 1024U,
         "timely delayed consumer did not receive complete child output");
  expect(run_child(fixture, "large_no_newline", "GAME\t0\tx\n") == 0,
         "large stderr line grew scanner storage or deadlocked");
  int concurrent_status_one = -1;
  int concurrent_status_two = -1;
  std::thread concurrent_one([&fixture, &concurrent_status_one]() -> void {
    concurrent_status_one = run_child(fixture, "normal", "GAME\t0\tx\n");
  });
  std::thread concurrent_two([&fixture, &concurrent_status_two]() -> void {
    concurrent_status_two = run_child(fixture, "normal", "GAME\t0\tx\n");
  });
  concurrent_one.join();
  concurrent_two.join();
  expect(concurrent_status_one == 0 && concurrent_status_two == 0,
         "concurrent relay signal masks interfered");
  expect(run_child(fixture, "long_step", DRAW) == 2, "oversized step record was accepted");
  expect(run_child(fixture, "exit0", DRAW) == 2, "early zero exit was accepted");
  expect(run_child(fixture, "exit7", DRAW) == 7, "early nonzero exit status was not preserved");
  expect(run_child(fixture, "signal", DRAW) == 137, "signal exit status was not preserved");
  expect(run_child(fixture, "partial", DRAW) == 2, "partial step record was accepted");
  const CapturedRun wrong_eid = run_captured(fixture, "wrong_eid", DRAW);
  expect(wrong_eid.status == 2, "wrong event id was accepted");
  expect(wrong_eid.error.find("FULL_ANALYZE_STEP\t1\tEVENT\t999") != std::string::npos,
         "protocol failure discarded the original stderr record");
  expect(run_child(fixture, "wrong_player", DRAW) == 2, "wrong player was accepted");
  expect(run_child(fixture, "duplicate", DISCARD) == 2, "duplicate player was accepted");
  constexpr std::array<std::string_view, 7> STRICT_FAILURES{
      "missing_field", "wrong_label",      "extra_field", "nan_duration",
      "inf_duration",  "zero_step_number", "skipped_step"};
  for (const std::string_view mode : STRICT_FAILURES) {
    expect(run_child(fixture, mode, DRAW) == 2, "malformed ten-field step was accepted");
  }
  expect(run_child(fixture, "delayed_duplicate", "ACTION\t0\tDRAW\t0\t1m\nGAME\t1\tx\n") == 2,
         "delayed duplicate step was forgotten between events");
  expect(run_child(fixture, "finish_duplicate", DRAW) == 2,
         "duplicate step emitted during finish was accepted");
  expect(timed_run(fixture, "hup_missing", DRAW, std::chrono::seconds(2)) == 2,
         "stdin HUP without a step was accepted");
  expect(timed_run(fixture, "ignore_term_bad", DRAW, std::chrono::seconds(2)) == 2,
         "SIGTERM-ignoring child stalled cleanup");
  expect(timed_run(fixture, "closed_sleep", "GAME\t0\tx\n", std::chrono::seconds(2)) == 2,
         "closed child pipes stalled finish");
  expect(run_without_reader(fixture, "stderr_bad") == 2,
         "full stderr destination stalled protocol cleanup");
  const auto descendant_start = std::chrono::steady_clock::now();
  const CapturedRun descendant = run_captured(fixture, "descendant", "GAME\t0\tx\n");
  expect(std::chrono::steady_clock::now() - descendant_start < std::chrono::seconds(2),
         "inherited descendant pipe stalled finish");
  expect(descendant.status == 0, "descendant changed direct child status");
  const std::string_view marker = "DESCENDANT_PID\t";
  const std::size_t marker_position = descendant.error.find(marker);
  expect(marker_position != std::string::npos, "descendant pid was not reported");
  const std::size_t pid_begin = marker_position + marker.size();
  const std::size_t pid_end = descendant.error.find('\n', pid_begin);
  pid_t descendant_pid = -1;
  const auto [parsed_end, parse_error] = std::from_chars(
      descendant.error.data() + pid_begin,
      descendant.error.data() + (pid_end == std::string::npos ? descendant.error.size() : pid_end),
      descendant_pid);
  expect(parse_error == std::errc{} &&
             parsed_end == descendant.error.data() +
                               (pid_end == std::string::npos ? descendant.error.size() : pid_end),
         "descendant pid was malformed");
  bool descendant_exists = true;
  for (int attempt = 0; attempt < 100 && descendant_exists; ++attempt) {
    descendant_exists = ::kill(descendant_pid, 0) == 0 || errno != ESRCH;
    if (descendant_exists) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  expect(!descendant_exists, "descendant remained after process-group cleanup");

  expect(run_child(fixture, "normal", "ROUND\t1\n") == 2, "incomplete related record was accepted");
  expect(run_child("/definitely/not/a/command", "normal", "") == 2, "missing command was accepted");
  Descriptor malformed = record_fd("ROUND\t1\nACTION\t2\tDRAW\n");
  full_analyze_interactor::EventReader malformed_reader(malformed.get());
  expect(!malformed_reader.next(), "wrong related record was accepted");
  Descriptor repeated = record_fd("GAME\t0\tx\nGAME\t0\tx\n");
  full_analyze_interactor::EventReader repeated_reader(repeated.get());
  expect(repeated_reader.next().has_value(), "first sequential event was rejected");
  expect(!repeated_reader.next(), "repeated event id was accepted");
  return 0;
}
