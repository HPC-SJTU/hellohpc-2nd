#include "interactor.hpp"

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace full_analyze_interactor {
namespace {

constexpr std::string_view STEP_PREFIX = "FULL_ANALYZE_STEP\t";
constexpr std::size_t MAX_STEP_LINE = 4096;
constexpr std::size_t MAX_INPUT_LINE = 1024U * 1024U;
constexpr std::size_t MAX_FORWARD_QUEUE = 1024U * 1024U;
constexpr auto TERMINATION_GRACE = std::chrono::milliseconds(300);
constexpr auto ORPHAN_DRAIN_GRACE = std::chrono::milliseconds(100);
constexpr auto CLOSED_PIPE_EXIT_GRACE = std::chrono::milliseconds(100);
constexpr auto OUTPUT_DELIVERY_GRACE = std::chrono::milliseconds(750);
constexpr auto FINISH_POLL_INTERVAL = std::chrono::milliseconds(20);

auto first_field(std::string_view line) -> std::string_view {
  return line.substr(0, line.find('\t'));
}

auto field(std::string_view line, std::size_t index) -> std::string_view {
  std::size_t begin = 0;
  for (std::size_t current = 0; current < index; ++current) {
    const std::size_t tab = line.find('\t', begin);
    if (tab == std::string_view::npos) return {};
    begin = tab + 1;
  }
  const std::size_t end = line.find('\t', begin);
  return line.substr(begin, end == std::string_view::npos ? end : end - begin);
}

auto valid_id(std::string_view id) -> bool {
  if (id.empty()) return false;
  long long value = -1;
  const auto [end, error] = std::from_chars(id.data(), id.data() + id.size(), value);
  return error == std::errc{} && end == id.data() + id.size() && value >= 0;
}

auto is_win(std::string_view line) -> bool {
  return first_field(line) == "WIN" || (first_field(line) == "ACTION" && field(line, 2) == "WIN");
}

auto is_drawn(std::string_view name) -> bool {
  return name == "DRAWN_EXHAUSTIVE" || name == "DRAWN_NINE_TERMINALS";
}

class Descriptor {
 public:
  Descriptor() = default;
  explicit Descriptor(int value) : value_(value) {}
  Descriptor(const Descriptor &) = delete;
  auto operator=(const Descriptor &) -> Descriptor & = delete;
  Descriptor(Descriptor &&other) noexcept : value_(std::exchange(other.value_, -1)) {}
  auto operator=(Descriptor &&other) noexcept -> Descriptor & {
    if (this != &other) {
      reset();
      value_ = std::exchange(other.value_, -1);
    }
    return *this;
  }
  ~Descriptor() { reset(); }
  auto get() const -> int { return value_; }
  auto open() const -> bool { return value_ >= 0; }
  auto reset() -> void {
    if (value_ >= 0) ::close(std::exchange(value_, -1));
  }

 private:
  int value_ = -1;
};

struct Pipe {
  Descriptor read;
  Descriptor write;
};

auto make_pipe(bool close_on_exec = false) -> std::expected<Pipe, std::string> {
  std::array<int, 2> values{};
  if (::pipe2(values.data(), close_on_exec ? O_CLOEXEC : 0) != 0) {
    return std::unexpected(std::strerror(errno));
  }
  return Pipe{Descriptor(values[0]), Descriptor(values[1])};
}

auto set_nonblocking(int descriptor) -> bool {
  const int flags = ::fcntl(descriptor, F_GETFL);
  return flags >= 0 && ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0;
}

class SignalSource {
 public:
  static auto create() -> std::expected<SignalSource, std::string> {
    sigset_t blocked{};
    ::sigemptyset(&blocked);
    ::sigaddset(&blocked, SIGINT);
    ::sigaddset(&blocked, SIGTERM);
    ::sigaddset(&blocked, SIGPIPE);
    sigset_t previous{};
    const int mask_error = ::pthread_sigmask(SIG_BLOCK, &blocked, &previous);
    if (mask_error != 0) {
      return std::unexpected(std::string("cannot block relay signals: ") +
                             std::strerror(mask_error));
    }
    const int descriptor = ::signalfd(-1, &blocked, SFD_CLOEXEC | SFD_NONBLOCK);
    if (descriptor < 0) {
      const int failure = errno;
      static_cast<void>(::pthread_sigmask(SIG_SETMASK, &previous, nullptr));
      return std::unexpected(std::string("cannot create signal descriptor: ") +
                             std::strerror(failure));
    }
    return SignalSource(Descriptor(descriptor), previous);
  }

  SignalSource(const SignalSource &) = delete;
  auto operator=(const SignalSource &) -> SignalSource & = delete;
  SignalSource(SignalSource &&other) noexcept
      : descriptor_(std::move(other.descriptor_)), previous_(other.previous_), active_(true) {
    other.active_ = false;
  }
  auto operator=(SignalSource &&) -> SignalSource & = delete;
  ~SignalSource() {
    drain();
    if (active_) static_cast<void>(::pthread_sigmask(SIG_SETMASK, &previous_, nullptr));
  }

  auto get() const -> int { return descriptor_.get(); }
  auto previous_mask() const -> const sigset_t & { return previous_; }

  auto received() -> std::expected<std::optional<int>, std::string> {
    while (true) {
      struct signalfd_siginfo information{};
      const ssize_t count = ::read(descriptor_.get(), &information, sizeof(information));
      if (count == static_cast<ssize_t>(sizeof(information))) {
        const int number = static_cast<int>(information.ssi_signo);
        if (number == SIGPIPE) continue;
        return std::optional<int>(number);
      }
      if (count < 0 && errno == EINTR) continue;
      if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return std::optional<int>{};
      }
      return std::unexpected(std::string("cannot read signal descriptor: ") + std::strerror(errno));
    }
  }

 private:
  Descriptor descriptor_;
  sigset_t previous_{};
  bool active_ = false;

  SignalSource(Descriptor descriptor, const sigset_t &previous)
      : descriptor_(std::move(descriptor)), previous_(previous), active_(true) {}

  auto drain() -> void {
    if (!descriptor_.open()) return;
    while (true) {
      struct signalfd_siginfo information{};
      const ssize_t count = ::read(descriptor_.get(), &information, sizeof(information));
      if (count == static_cast<ssize_t>(sizeof(information)) || (count < 0 && errno == EINTR)) {
        continue;
      }
      return;
    }
  }
};

class ProtocolScanner {
 public:
  auto begin_event(std::string_view id, std::vector<int> expected)
      -> std::expected<void, std::string> {
    if (expectation_ && !expectation_->complete()) {
      return std::unexpected("cannot begin an event before its FULL_ANALYZE_STEP records complete");
    }
    expectation_.emplace(std::string(id), std::move(expected));
    return {};
  }

  auto consume(std::string_view bytes) -> std::expected<void, std::string> {
    for (const char byte : bytes) {
      if (byte == '\n') {
        if (candidate_ && prefix_position_ == STEP_PREFIX.size()) {
          auto parsed = parse_line();
          if (!parsed) return parsed;
        }
        reset_line();
        continue;
      }
      if (!candidate_) continue;
      if (prefix_position_ < STEP_PREFIX.size()) {
        if (byte != STEP_PREFIX[prefix_position_]) {
          candidate_ = false;
          line_.clear();
          continue;
        }
        line_.push_back(byte);
        ++prefix_position_;
        continue;
      }
      if (line_.size() == MAX_STEP_LINE) {
        return std::unexpected("FULL_ANALYZE_STEP record exceeds 4096 bytes");
      }
      line_.push_back(byte);
    }
    return {};
  }

  auto complete() const -> bool { return expectation_ && expectation_->complete(); }

 private:
  struct Expectation {
    std::string id;
    std::vector<int> players;
    std::array<bool, 4> seen{};

    Expectation(std::string event_id, std::vector<int> expected_players)
        : id(std::move(event_id)), players(std::move(expected_players)) {}

    auto complete() const -> bool {
      return std::ranges::all_of(
          players, [this](int player) -> bool { return seen[static_cast<std::size_t>(player)]; });
    }
  };

  std::optional<Expectation> expectation_;
  std::string line_;
  std::size_t prefix_position_ = 0;
  std::size_t next_step_ = 1;
  bool candidate_ = true;

  auto reset_line() -> void {
    line_.clear();
    prefix_position_ = 0;
    candidate_ = true;
  }

  auto parse_line() -> std::expected<void, std::string> {
    std::array<std::string_view, 10> fields{};
    std::size_t begin = 0;
    for (std::size_t index = 0; index < fields.size(); ++index) {
      const std::size_t end = line_.find('\t', begin);
      fields[index] =
          std::string_view(line_).substr(begin, end == std::string::npos ? end : end - begin);
      if ((index + 1 < fields.size()) != (end != std::string::npos)) {
        return std::unexpected("malformed FULL_ANALYZE_STEP record");
      }
      if (end == std::string::npos) continue;
      begin = end + 1;
    }
    if (fields[0] != "FULL_ANALYZE_STEP" || fields[2] != "EVENT" || fields[4] != "PLAYER" ||
        fields[6] != "ELAPSED_SECONDS" || fields[8] != "STEP_SECONDS") {
      return std::unexpected("malformed FULL_ANALYZE_STEP record");
    }
    std::size_t step = 0;
    const auto [step_end, step_error] =
        std::from_chars(fields[1].data(), fields[1].data() + fields[1].size(), step);
    if (step_error != std::errc{} || step_end != fields[1].data() + fields[1].size() || step == 0 ||
        step != next_step_) {
      return std::unexpected(std::string("FULL_ANALYZE_STEP has step ").append(fields[1]) +
                             ", expected " + std::to_string(next_step_));
    }
    double elapsed = 0.0;
    double duration = 0.0;
    const auto [elapsed_end, elapsed_error] = std::from_chars(
        fields[7].data(), fields[7].data() + fields[7].size(), elapsed, std::chars_format::general);
    const auto [duration_end, duration_error] =
        std::from_chars(fields[9].data(), fields[9].data() + fields[9].size(), duration,
                        std::chars_format::general);
    if (elapsed_error != std::errc{} || elapsed_end != fields[7].data() + fields[7].size() ||
        duration_error != std::errc{} || duration_end != fields[9].data() + fields[9].size() ||
        !std::isfinite(elapsed) || !std::isfinite(duration)) {
      return std::unexpected("FULL_ANALYZE_STEP has an invalid duration");
    }
    if (!expectation_) {
      return std::unexpected("unexpected FULL_ANALYZE_STEP without an event");
    }
    if (fields[3] != expectation_->id) {
      return std::unexpected(std::string("FULL_ANALYZE_STEP has event ").append(fields[3]) +
                             ", expected " + expectation_->id);
    }
    int player = -1;
    const auto [end, error] =
        std::from_chars(fields[5].data(), fields[5].data() + fields[5].size(), player);
    if (error != std::errc{} || end != fields[5].data() + fields[5].size() || player < 0 ||
        player >= 4) {
      return std::unexpected("FULL_ANALYZE_STEP has an invalid player");
    }
    if (std::find(expectation_->players.begin(), expectation_->players.end(), player) ==
        expectation_->players.end()) {
      return std::unexpected("unexpected FULL_ANALYZE_STEP player " + std::to_string(player));
    }
    if (expectation_->seen[static_cast<std::size_t>(player)]) {
      return std::unexpected("duplicate FULL_ANALYZE_STEP player " + std::to_string(player));
    }
    expectation_->seen[static_cast<std::size_t>(player)] = true;
    ++next_step_;
    return {};
  }
};

struct Child {
  pid_t pid = -1;
  Descriptor input;
  Descriptor output;
  Descriptor error;
  std::optional<int> exit_code;
};

auto decode_status(int status) -> int {
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
  return 2;
}

auto check_child(Child &child) -> std::expected<bool, std::string> {
  if (child.exit_code) return true;
  int status = 0;
  const pid_t result = ::waitpid(child.pid, &status, WNOHANG);
  if (result == child.pid) {
    child.exit_code = decode_status(status);
    return true;
  }
  if (result == 0) return false;
  if (errno == EINTR) return false;
  return std::unexpected(std::string("waitpid failed: ") + std::strerror(errno));
}

auto kill_and_reap(Child &child) -> void {
  static_cast<void>(::kill(-child.pid, SIGKILL));
  static_cast<void>(::kill(child.pid, SIGKILL));
  const auto deadline = std::chrono::steady_clock::now() + TERMINATION_GRACE;
  while (!child.exit_code && std::chrono::steady_clock::now() < deadline) {
    auto exited = check_child(child);
    if (!exited || *exited) return;
    struct timespec pause{0, 1000000};
    static_cast<void>(::nanosleep(&pause, nullptr));
  }
}

auto spawn(std::span<const std::string_view> command, const SignalSource &signals)
    -> std::expected<Child, std::string> {
  auto input_result = make_pipe(true);
  auto output_result = make_pipe(true);
  auto error_result = make_pipe(true);
  auto exec_result = make_pipe(true);
  if (!input_result || !output_result || !error_result || !exec_result) {
    const std::string &reason = !input_result    ? input_result.error()
                                : !output_result ? output_result.error()
                                : !error_result  ? error_result.error()
                                                 : exec_result.error();
    return std::unexpected("cannot create pipes: " + reason);
  }
  Pipe input = std::move(*input_result);
  Pipe output = std::move(*output_result);
  Pipe error = std::move(*error_result);
  Pipe exec_status = std::move(*exec_result);

  std::vector<std::string> owned_arguments;
  owned_arguments.reserve(command.size());
  for (const std::string_view argument : command) owned_arguments.emplace_back(argument);
  std::vector<char *> arguments;
  arguments.reserve(owned_arguments.size() + 1);
  for (std::string &argument : owned_arguments) arguments.push_back(argument.data());
  arguments.push_back(nullptr);
  const sigset_t child_signal_mask = signals.previous_mask();

  const pid_t pid = ::fork();
  if (pid < 0) return std::unexpected(std::string("cannot fork: ") + std::strerror(errno));
  if (pid == 0) {
    if (::setpgid(0, 0) != 0) {
      const int failure = errno;
      static_cast<void>(::write(exec_status.write.get(), &failure, sizeof(failure)));
      _exit(127);
    }
    static_cast<void>(::sigprocmask(SIG_SETMASK, &child_signal_mask, nullptr));
    static_cast<void>(::close(exec_status.read.get()));
    if (::dup2(input.read.get(), STDIN_FILENO) < 0 ||
        ::dup2(output.write.get(), STDOUT_FILENO) < 0 ||
        ::dup2(error.write.get(), STDERR_FILENO) < 0) {
      const int failure = errno;
      static_cast<void>(::write(exec_status.write.get(), &failure, sizeof(failure)));
      _exit(127);
    }
    static_cast<void>(::close(input.read.get()));
    static_cast<void>(::close(input.write.get()));
    static_cast<void>(::close(output.read.get()));
    static_cast<void>(::close(output.write.get()));
    static_cast<void>(::close(error.read.get()));
    static_cast<void>(::close(error.write.get()));
    ::execvp(arguments[0], arguments.data());
    const int failure = errno;
    static_cast<void>(::write(exec_status.write.get(), &failure, sizeof(failure)));
    _exit(127);
  }

  input.read.reset();
  output.write.reset();
  error.write.reset();
  exec_status.write.reset();
  if (::setpgid(pid, pid) != 0 && errno != EACCES && errno != ESRCH) {
    const int failure = errno;
    static_cast<void>(::kill(-pid, SIGKILL));
    static_cast<void>(::kill(pid, SIGKILL));
    static_cast<void>(::waitpid(pid, nullptr, 0));
    return std::unexpected(std::string("cannot create submission process group: ") +
                           std::strerror(failure));
  }
  int exec_error = 0;
  ssize_t count = -1;
  do {
    count = ::read(exec_status.read.get(), &exec_error, sizeof(exec_error));
  } while (count < 0 && errno == EINTR);
  if (count > 0) {
    input.write.reset();
    output.read.reset();
    error.read.reset();
    static_cast<void>(::waitpid(pid, nullptr, 0));
    return std::unexpected(std::string("cannot execute submission command: ") +
                           std::strerror(exec_error));
  }
  if (count < 0) {
    static_cast<void>(::kill(-pid, SIGKILL));
    static_cast<void>(::waitpid(pid, nullptr, 0));
    return std::unexpected("cannot confirm submission command execution");
  }
  if (!set_nonblocking(input.write.get()) || !set_nonblocking(output.read.get()) ||
      !set_nonblocking(error.read.get())) {
    static_cast<void>(::kill(-pid, SIGKILL));
    static_cast<void>(::waitpid(pid, nullptr, 0));
    return std::unexpected("cannot configure child pipes");
  }
  return Child{pid, std::move(input.write), std::move(output.read), std::move(error.read),
               std::nullopt};
}

class Destination {
 public:
  Destination() = default;
  Destination(Descriptor descriptor, bool socket)
      : descriptor_(std::move(descriptor)), socket_(socket) {}
  Destination(const Destination &) = delete;
  auto operator=(const Destination &) -> Destination & = delete;
  Destination(Destination &&) noexcept = default;
  auto operator=(Destination &&) noexcept -> Destination & = default;
  ~Destination() = default;
  auto get() const -> int { return descriptor_.get(); }
  auto reset() -> void { descriptor_.reset(); }
  auto write(std::string_view bytes) const -> ssize_t {
    return socket_
               ? ::send(descriptor_.get(), bytes.data(), bytes.size(), MSG_DONTWAIT | MSG_NOSIGNAL)
               : ::write(descriptor_.get(), bytes.data(), bytes.size());
  }

  static auto create(int original) -> std::expected<Destination, std::string> {
    const int flags = ::fcntl(original, F_GETFL);
    if (flags < 0) {
      return std::unexpected(std::string("cannot inspect destination: ") + std::strerror(errno));
    }
    struct stat status{};
    if (::fstat(original, &status) != 0) {
      return std::unexpected(std::string("cannot inspect destination: ") + std::strerror(errno));
    }
    if (S_ISREG(status.st_mode)) {
      const int duplicate = ::fcntl(original, F_DUPFD_CLOEXEC, 0);
      if (duplicate < 0) {
        return std::unexpected(std::string("cannot duplicate destination: ") +
                               std::strerror(errno));
      }
      return Destination(Descriptor(duplicate), false);
    }
    if (S_ISSOCK(status.st_mode)) {
      const int duplicate = ::fcntl(original, F_DUPFD_CLOEXEC, 0);
      if (duplicate < 0) {
        return std::unexpected(std::string("cannot duplicate destination: ") +
                               std::strerror(errno));
      }
      return Destination(Descriptor(duplicate), true);
    }
    const std::string path = "/proc/self/fd/" + std::to_string(original);
    const int duplicate = ::open(path.c_str(), (flags & O_ACCMODE) | O_NONBLOCK | O_CLOEXEC);
    if (duplicate < 0) {
      const int failure = errno;
      return std::unexpected(std::string("cannot configure destination: ") +
                             std::strerror(failure));
    }
    return Destination(Descriptor(duplicate), false);
  }

 private:
  Descriptor descriptor_;
  bool socket_ = false;
};

struct ForwardStream {
  Descriptor *source = nullptr;
  Destination destination;
  std::string queue;
  std::size_t offset = 0;
  std::optional<std::chrono::steady_clock::time_point> delivery_deadline;

  auto pending() const -> std::size_t { return queue.size() - offset; }
  auto can_read() const -> bool { return source->open() && pending() < MAX_FORWARD_QUEUE; }
  auto has_pending() const -> bool { return pending() != 0; }

  auto append(std::string_view bytes) -> void {
    const bool was_empty = !has_pending();
    if (offset != 0 && (offset >= queue.size() / 2 || queue.size() == queue.capacity())) {
      queue.erase(0, offset);
      offset = 0;
    }
    queue.append(bytes);
    if (was_empty && !bytes.empty()) {
      delivery_deadline = std::chrono::steady_clock::now() + OUTPUT_DELIVERY_GRACE;
    }
  }

  auto flush() -> std::expected<void, std::string> {
    while (has_pending()) {
      const ssize_t count = destination.write(std::string_view(queue).substr(offset, pending()));
      if (count > 0) {
        offset += static_cast<std::size_t>(count);
        delivery_deadline = std::chrono::steady_clock::now() + OUTPUT_DELIVERY_GRACE;
      } else if (count < 0 && errno == EINTR) {
        continue;
      } else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return {};
      } else {
        return std::unexpected(std::string("cannot forward child output: ") + std::strerror(errno));
      }
    }
    queue.clear();
    offset = 0;
    delivery_deadline.reset();
    return {};
  }

  auto delivery_expired(std::chrono::steady_clock::time_point now) const -> bool {
    return has_pending() && delivery_deadline && now >= *delivery_deadline;
  }

  auto read(ProtocolScanner *scanner) -> std::expected<void, std::string> {
    std::array<char, 16384> buffer{};
    while (can_read()) {
      const ssize_t count = ::read(source->get(), buffer.data(), buffer.size());
      if (count > 0) {
        const std::string_view bytes(buffer.data(), static_cast<std::size_t>(count));
        append(bytes);
        if (scanner != nullptr) {
          auto scanned = scanner->consume(bytes);
          if (!scanned) return scanned;
        }
      } else if (count == 0) {
        source->reset();
        return {};
      } else if (errno == EINTR) {
        continue;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return {};
      } else {
        return std::unexpected(std::string("cannot read child output: ") + std::strerror(errno));
      }
    }
    return {};
  }
};

class Relay {
 public:
  static auto create(Child &child, SignalSource &signals, const RunOptions &options)
      -> std::expected<Relay, std::string> {
    auto output = Destination::create(options.output_fd);
    if (!output) return std::unexpected(std::move(output).error());
    auto error = Destination::create(options.error_fd);
    if (!error) return std::unexpected(std::move(error).error());
    return Relay(child, signals, std::move(*output), std::move(*error));
  }

  auto send(const Event &event, int mask) -> std::expected<void, std::string> {
    auto begun = scanner_.begin_event(event.id, expected_players(event, mask));
    if (!begun) return begun;
    std::size_t written = 0;
    while (written != event.text.size() || !scanner_.complete()) {
      auto signal_result = forward_signal();
      if (!signal_result) return signal_result;
      auto pumped = pump(written < event.text.size(), -1, true);
      if (!pumped) return std::unexpected(std::move(pumped).error());

      if (written < event.text.size() && (last_input_events_ & POLLOUT) != 0) {
        const ssize_t count =
            ::write(child_.input.get(), event.text.data() + written, event.text.size() - written);
        if (count > 0) {
          written += static_cast<std::size_t>(count);
        } else if (count < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
          child_.input.reset();
          return std::unexpected(
              errno == EPIPE ? "submission exited while receiving event " + event.id
                             : std::string("cannot write child input: ") + std::strerror(errno));
        }
      }
      auto exited = check_child(child_);
      if (!exited) return std::unexpected(std::move(exited).error());
      if (*exited && !scanner_.complete()) {
        return std::unexpected("submission exited with status " +
                               std::to_string(*child_.exit_code) +
                               " before all FULL_ANALYZE_STEP records for event " + event.id);
      }
      if ((last_input_events_ & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        child_.input.reset();
        if (!*exited) {
          auto settled = pump(false, 20, true);
          if (!settled) return std::unexpected(std::move(settled).error());
          exited = check_child(child_);
          if (!exited) return std::unexpected(std::move(exited).error());
          if (*exited && !scanner_.complete()) {
            return std::unexpected("submission exited with status " +
                                   std::to_string(*child_.exit_code) +
                                   " before all FULL_ANALYZE_STEP records for event " + event.id);
          }
        }
        return std::unexpected(
            written < event.text.size()
                ? "submission closed stdin while receiving event " + event.id
                : "submission closed stdin before all FULL_ANALYZE_STEP records for event " +
                      event.id);
      }
    }
    return {};
  }

  auto finish() -> std::expected<int, std::string> {
    child_.input.reset();
    std::optional<std::chrono::steady_clock::time_point> orphan_deadline;
    std::optional<std::chrono::steady_clock::time_point> closed_pipe_deadline;
    bool orphan_cleanup_done = false;
    while (!child_.exit_code || child_.output.open() || child_.error.open() ||
           output_.has_pending() || error_.has_pending()) {
      auto signal_result = forward_signal();
      if (!signal_result) return std::unexpected(std::move(signal_result).error());
      auto exited = check_child(child_);
      if (!exited) return std::unexpected(std::move(exited).error());
      if (*exited && !orphan_deadline && !orphan_cleanup_done &&
          (child_.output.open() || child_.error.open())) {
        orphan_deadline = std::chrono::steady_clock::now() + ORPHAN_DRAIN_GRACE;
      }
      if (orphan_deadline && std::chrono::steady_clock::now() >= *orphan_deadline) {
        signal_group(SIGKILL);
        auto drained = drain_until(std::chrono::steady_clock::now() + TERMINATION_GRACE, true);
        if (!drained) return std::unexpected(std::move(drained).error());
        output_.source->reset();
        error_.source->reset();
        orphan_deadline.reset();
        orphan_cleanup_done = true;
        continue;
      }
      if (!*exited && !child_.output.open() && !child_.error.open()) {
        if (!closed_pipe_deadline) {
          closed_pipe_deadline = std::chrono::steady_clock::now() + CLOSED_PIPE_EXIT_GRACE;
        } else if (std::chrono::steady_clock::now() >= *closed_pipe_deadline) {
          signal_group(SIGTERM);
          static_cast<void>(
              drain_until(std::chrono::steady_clock::now() + TERMINATION_GRACE, false));
          signal_group(SIGKILL);
          static_cast<void>(
              drain_until(std::chrono::steady_clock::now() + TERMINATION_GRACE, false));
          reap_until(std::chrono::steady_clock::now() + TERMINATION_GRACE);
          return std::unexpected("submission closed its output pipes but did not exit");
        }
      }
      int timeout = static_cast<int>(FINISH_POLL_INTERVAL.count());
      if (orphan_deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            *orphan_deadline - std::chrono::steady_clock::now());
        timeout = std::max(1, std::min(timeout, static_cast<int>(remaining.count())));
      }
      auto pumped = pump(false, timeout, true);
      if (!pumped) return std::unexpected(std::move(pumped).error());
    }
    if (!child_.exit_code) return std::unexpected("submission did not exit before deadline");
    return *child_.exit_code;
  }

  auto terminate() -> void {
    child_.input.reset();
    signal_group(SIGTERM);
    const auto term_deadline = std::chrono::steady_clock::now() + TERMINATION_GRACE;
    static_cast<void>(drain_until(term_deadline, false));
    signal_group(SIGKILL);
    const auto kill_deadline = std::chrono::steady_clock::now() + TERMINATION_GRACE;
    static_cast<void>(drain_until(kill_deadline, false));
    reap_until(kill_deadline);
    if (!diagnostic_.empty()) {
      error_.append(diagnostic_);
      static_cast<void>(
          drain_until(std::chrono::steady_clock::now() + OUTPUT_DELIVERY_GRACE, false));
    }
    close_streams();
  }

  auto report(std::string_view category, std::string_view message) -> void {
    diagnostic_.reserve(diagnostic_.size() + category.size() + message.size() + 3);
    diagnostic_.append(category);
    diagnostic_.append(": ");
    diagnostic_.append(message);
    diagnostic_.push_back('\n');
  }

 private:
  Child &child_;
  SignalSource &signals_;
  ProtocolScanner scanner_;
  ForwardStream output_;
  ForwardStream error_;
  short last_input_events_ = 0;
  std::string diagnostic_;

  Relay(Child &child, SignalSource &signals, Destination output, Destination error)
      : child_(child),
        signals_(signals),
        output_{&child.output, std::move(output), {}, 0, std::nullopt},
        error_{&child.error, std::move(error), {}, 0, std::nullopt} {}

  static auto expected_players(const Event &event, int mask) -> std::vector<int> {
    const std::string_view type = field(event.text, 2);
    if (type != "DRAW" && type != "DISCARD" && type != "UPGRADED_KAN") return {};
    int actor = -1;
    const std::string_view actor_text = field(event.text, 3);
    const auto [end, error] =
        std::from_chars(actor_text.data(), actor_text.data() + actor_text.size(), actor);
    if (error != std::errc{} || end != actor_text.data() + actor_text.size() || actor < 0 ||
        actor >= 4) {
      return {};
    }
    std::vector<int> players;
    if (type == "DRAW") {
      if ((mask & (1 << actor)) != 0) players.push_back(actor);
    } else {
      for (int player = 0; player < 4; ++player) {
        if ((mask & (1 << player)) != 0 && player != actor) players.push_back(player);
      }
    }
    return players;
  }

  auto forward_signal() -> std::expected<void, std::string> {
    auto received = signals_.received();
    if (!received) return std::unexpected(std::move(received).error());
    if (!*received) return {};
    const int signal_number = **received;
    if (::kill(-child_.pid, signal_number) != 0 && errno != ESRCH) {
      return std::unexpected(std::string("cannot signal submission: ") + std::strerror(errno));
    }
    return std::unexpected("interrupted by signal " + std::to_string(signal_number));
  }

  auto pump(bool want_input, int timeout, bool scan_protocol) -> std::expected<void, std::string> {
    const auto before = std::chrono::steady_clock::now();
    if (output_.delivery_expired(before) || error_.delivery_expired(before)) {
      return std::unexpected("cannot deliver child output before deadline");
    }
    const auto limit_timeout = [before, &timeout](const ForwardStream &stream) -> void {
      if (!stream.has_pending() || !stream.delivery_deadline) return;
      const auto remaining =
          std::chrono::duration_cast<std::chrono::milliseconds>(*stream.delivery_deadline - before);
      const int delivery_timeout = std::max(1, static_cast<int>(remaining.count()));
      if (timeout < 0 || delivery_timeout < timeout) timeout = delivery_timeout;
    };
    limit_timeout(output_);
    limit_timeout(error_);
    std::array<struct pollfd, 6> descriptors{{
        {child_.input.open() ? child_.input.get() : -1,
         static_cast<short>(want_input ? POLLOUT : 0), 0},
        {output_.can_read() ? child_.output.get() : -1, POLLIN, 0},
        {error_.can_read() ? child_.error.get() : -1, POLLIN, 0},
        {output_.has_pending() ? output_.destination.get() : -1, POLLOUT, 0},
        {error_.has_pending() ? error_.destination.get() : -1, POLLOUT, 0},
        {signals_.get(), POLLIN, 0},
    }};
    const int ready = ::poll(descriptors.data(), descriptors.size(), timeout);
    if (ready < 0) {
      if (errno == EINTR) return {};
      return std::unexpected(std::string("poll failed: ") + std::strerror(errno));
    }
    last_input_events_ = descriptors[0].revents;
    if (descriptors[5].revents != 0) {
      auto signal_result = forward_signal();
      if (!signal_result) return signal_result;
    }
    if (descriptors[3].revents != 0) {
      auto result = output_.flush();
      if (!result) return result;
    }
    if (descriptors[4].revents != 0) {
      auto result = error_.flush();
      if (!result) return result;
    }
    if (descriptors[1].revents != 0) {
      auto result = output_.read(nullptr);
      if (!result) return result;
    }
    if (descriptors[2].revents != 0) {
      auto result = error_.read(scan_protocol ? &scanner_ : nullptr);
      if (!result) return result;
    }
    const auto after = std::chrono::steady_clock::now();
    if (output_.delivery_expired(after) || error_.delivery_expired(after)) {
      return std::unexpected("cannot deliver child output before deadline");
    }
    return {};
  }

  auto drain_until(std::chrono::steady_clock::time_point deadline, bool scan_protocol)
      -> std::expected<void, std::string> {
    while (std::chrono::steady_clock::now() < deadline) {
      auto exited = check_child(child_);
      if (!exited) return std::unexpected(std::move(exited).error());
      if (*exited && !child_.output.open() && !child_.error.open() && !output_.has_pending() &&
          !error_.has_pending()) {
        return {};
      }
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - std::chrono::steady_clock::now());
      const int timeout = std::max(1, static_cast<int>(remaining.count()));
      auto pumped = pump(false, timeout, scan_protocol);
      if (!pumped) return pumped;
    }
    return {};
  }

  auto signal_group(int signal_number) const -> void {
    if (::kill(-child_.pid, signal_number) != 0 && errno != ESRCH) return;
  }

  auto reap_until(std::chrono::steady_clock::time_point deadline) -> void {
    while (!child_.exit_code && std::chrono::steady_clock::now() < deadline) {
      auto exited = check_child(child_);
      if (!exited || *exited) return;
      struct pollfd signal_descriptor{signals_.get(), POLLIN, 0};
      static_cast<void>(::poll(&signal_descriptor, 1, 10));
    }
  }

  auto close_streams() -> void {
    output_.source->reset();
    error_.source->reset();
    output_.destination.reset();
    error_.destination.reset();
  }
};

auto report(int descriptor, std::string_view category, std::string_view message) -> void {
  std::string output;
  output.reserve(category.size() + message.size() + 3);
  output.append(category);
  output.append(": ");
  output.append(message);
  output.push_back('\n');
  auto destination_result = Destination::create(descriptor);
  if (!destination_result) return;
  Destination destination = std::move(*destination_result);
  std::size_t offset = 0;
  const auto deadline = std::chrono::steady_clock::now() + OUTPUT_DELIVERY_GRACE;
  while (offset != output.size() && std::chrono::steady_clock::now() < deadline) {
    const ssize_t count = destination.write(std::string_view(output).substr(offset));
    if (count > 0) {
      offset += static_cast<std::size_t>(count);
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return;
    struct pollfd writable{destination.get(), POLLOUT, 0};
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    static_cast<void>(::poll(&writable, 1, std::max(1, static_cast<int>(remaining.count()))));
  }
}

}  // namespace

auto EventReader::physical_line() -> std::expected<std::optional<std::string>, std::string> {
  std::size_t newline = buffer_.find('\n', buffer_offset_);
  while (newline == std::string::npos && !eof_) {
    if (buffer_.size() - buffer_offset_ > MAX_INPUT_LINE) {
      return std::unexpected("line " + std::to_string(line_number_ + 1) +
                             ": record exceeds 1048576 bytes");
    }
    if (buffer_offset_ != 0) {
      buffer_.erase(0, buffer_offset_);
      buffer_offset_ = 0;
    }
    std::array<char, 65536> bytes{};
    const ssize_t count = ::read(input_fd_, bytes.data(), bytes.size());
    if (count > 0) {
      buffer_.append(bytes.data(), static_cast<std::size_t>(count));
    } else if (count == 0) {
      eof_ = true;
    } else if (errno != EINTR) {
      return std::unexpected(std::string("failed to read the input file: ") + std::strerror(errno));
    }
    newline = buffer_.find('\n', buffer_offset_);
  }
  if (newline == std::string::npos) {
    if (buffer_offset_ == buffer_.size()) return std::optional<std::string>{};
    ++line_number_;
    return std::unexpected("line " + std::to_string(line_number_) +
                           ": the final record has no LF character");
  }
  if (newline - buffer_offset_ > MAX_INPUT_LINE) {
    return std::unexpected("line " + std::to_string(line_number_ + 1) +
                           ": record exceeds 1048576 bytes");
  }
  std::string line = buffer_.substr(buffer_offset_, newline - buffer_offset_);
  buffer_offset_ = newline + 1;
  ++line_number_;
  if (line.empty()) {
    return std::unexpected("line " + std::to_string(line_number_) +
                           ": empty records are not permitted");
  }
  if (line.back() == '\r') {
    return std::unexpected("line " + std::to_string(line_number_) +
                           ": CR characters are not permitted");
  }
  return std::optional<std::string>(std::move(line));
}

auto EventReader::next() -> std::expected<std::optional<Event>, std::string> {
  auto first_result = physical_line();
  if (!first_result) return std::unexpected(std::move(first_result).error());
  if (!*first_result) return std::optional<Event>{};
  std::string first = std::move(**first_result);
  const std::size_t first_number = line_number_;
  const std::string_view id = field(first, 1);
  if (!valid_id(id)) {
    return std::unexpected("line " + std::to_string(first_number) + ": invalid event id");
  }
  unsigned long long numeric_id = 0;
  const auto [id_end, id_error] = std::from_chars(id.data(), id.data() + id.size(), numeric_id);
  if (id_error != std::errc{} || id_end != id.data() + id.size() || numeric_id != next_event_id_) {
    return std::unexpected(("line " + std::to_string(first_number) + ": event id ").append(id) +
                           " is not the expected " + std::to_string(next_event_id_));
  }
  Event event;
  event.text.reserve(first.size() + 1);
  event.text.append(first);
  event.text.push_back('\n');
  event.id = id;
  event.first_line = first_number;
  const std::string_view name = first_field(first);
  std::size_t related_count = 0;
  std::string_view related_name;
  if (name == "ROUND") {
    related_count = 4;
    related_name = "HAND";
  } else if (is_win(first)) {
    related_count = 1;
    related_name = "WIN_RESULT";
  } else if (is_drawn(name)) {
    related_count = 4;
    related_name = "HAND";
  }
  for (std::size_t index = 0; index < related_count; ++index) {
    auto related_result = physical_line();
    if (!related_result) return std::unexpected(std::move(related_result).error());
    if (!*related_result) {
      return std::unexpected(("line " + std::to_string(first_number) + ": incomplete ")
                                 .append(name)
                                 .append(" event: expected ")
                                 .append(related_name));
    }
    std::string related = std::move(**related_result);
    if (first_field(related) != related_name || field(related, 1) != event.id) {
      return std::unexpected(
          ("line " + std::to_string(line_number_) + ": expected related ").append(related_name) +
          " for event " + event.id);
    }
    event.text.append(related);
    event.text.push_back('\n');
  }
  ++next_event_id_;
  return std::optional<Event>(std::move(event));
}

auto run(int record_fd, int mask, std::span<const std::string_view> command,
         const RunOptions &options) -> int {
  if (command.empty()) {
    report(options.error_fd, "interactor", "submission command is empty");
    return 2;
  }
  auto signal_result = SignalSource::create();
  if (!signal_result) {
    report(options.error_fd, "interactor", signal_result.error());
    return 2;
  }
  SignalSource signals = std::move(*signal_result);
  auto child_result = spawn(command, signals);
  if (!child_result) {
    report(options.error_fd, "interactor", child_result.error());
    return 2;
  }
  Child child = std::move(*child_result);
  auto relay_result = Relay::create(child, signals, options);
  if (!relay_result) {
    report(options.error_fd, "interactor", relay_result.error());
    kill_and_reap(child);
    return 2;
  }
  Relay relay = std::move(*relay_result);
  EventReader reader(record_fd);
  while (true) {
    auto event_result = reader.next();
    if (!event_result) {
      relay.report("game-record", event_result.error());
      relay.terminate();
      return 2;
    }
    if (!*event_result) break;
    auto sent = relay.send(**event_result, mask);
    if (!sent) {
      relay.report("interactor", sent.error());
      const std::optional<int> status = child.exit_code;
      relay.terminate();
      return status && *status != 0 ? *status : 2;
    }
  }
  auto status = relay.finish();
  if (!status) {
    relay.report("interactor", status.error());
    relay.terminate();
    return 2;
  }
  return *status;
}

}  // namespace full_analyze_interactor
