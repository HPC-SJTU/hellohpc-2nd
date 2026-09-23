#include <poll.h>
#include <signal.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>

namespace {

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

auto write_all(int descriptor, std::string_view bytes) -> bool {
  while (!bytes.empty()) {
    const ssize_t count = ::write(descriptor, bytes.data(), bytes.size());
    if (count > 0) {
      bytes.remove_prefix(static_cast<std::size_t>(count));
    } else if (count < 0 && errno == EINTR) {
      continue;
    } else {
      return false;
    }
  }
  return true;
}

auto write_repeated(int descriptor, char byte, std::size_t size) -> bool {
  std::array<char, 8192> data{};
  data.fill(byte);
  while (size != 0) {
    const std::size_t amount = std::min(size, data.size());
    const ssize_t count = ::write(descriptor, data.data(), amount);
    if (count > 0) {
      size -= static_cast<std::size_t>(count);
    } else if (count < 0 && errno == EINTR) {
      continue;
    } else {
      return false;
    }
  }
  return true;
}

class LineReader {
 public:
  auto next() -> std::string {
    while (true) {
      const std::size_t newline = buffer_.find('\n');
      if (newline != std::string::npos) {
        std::string line = buffer_.substr(0, newline);
        buffer_.erase(0, newline + 1);
        return line;
      }
      std::array<char, 4096> bytes{};
      const ssize_t count = ::read(STDIN_FILENO, bytes.data(), bytes.size());
      if (count > 0) {
        buffer_.append(bytes.data(), static_cast<std::size_t>(count));
      } else if (count < 0 && errno == EINTR) {
        continue;
      } else {
        return {};
      }
    }
  }

  auto buffered() const -> bool { return !buffer_.empty(); }

 private:
  std::string buffer_;
};

std::size_t next_step = 1;

auto step(std::string_view id, int player) -> bool {
  std::string record;
  record.reserve(128);
  record.append("FULL_ANALYZE_STEP\t");
  record.append(std::to_string(next_step));
  record.append("\tEVENT\t");
  record.append(id);
  record.append("\tPLAYER\t");
  record.append(std::to_string(player));
  record.append("\tELAPSED_SECONDS\t0.25\tSTEP_SECONDS\t1e-3\n");
  ++next_step;
  return write_all(STDERR_FILENO, record);
}

auto raw_step(std::string_view record) -> bool { return write_all(STDERR_FILENO, record); }

auto extra_lines(std::string_view line) -> std::size_t {
  const std::string_view name = field(line, 0);
  if (name == "ROUND") return 4;
  if (name == "ACTION" && field(line, 2) == "WIN") return 1;
  if (name == "DRAWN_EXHAUSTIVE" || name == "DRAWN_NINE_TERMINALS") return 4;
  return 0;
}

}  // namespace

auto main(int argc, char *argv[]) -> int {
  const std::string_view mode = argc > 1 ? argv[1] : "normal";
  if (mode == "exit0") return 0;
  if (mode == "exit7") return 7;
  if (mode == "signal") {
    ::raise(SIGKILL);
    return 90;
  }
  if (mode == "closed_sleep") {
    ::close(STDOUT_FILENO);
    ::close(STDERR_FILENO);
    ::signal(SIGTERM, SIG_IGN);
    ::sleep(10);
    return 91;
  }
  LineReader input;
  std::size_t event_count = 0;
  std::string previous_id;
  while (true) {
    std::string line = input.next();
    if (line.empty()) break;
    ++event_count;
    const std::string id(field(line, 1));
    const std::string_view name = field(line, 0);
    for (std::size_t index = 0; index < extra_lines(line); ++index) {
      if (input.next().empty()) return 80;
    }
    const bool needs_step =
        name == "ACTION" && (field(line, 2) == "DRAW" || field(line, 2) == "DISCARD" ||
                             field(line, 2) == "UPGRADED_KAN");
    if (mode == "sync" && needs_step) {
      if (input.buffered()) return 81;
      struct pollfd descriptor{STDIN_FILENO, POLLIN, 0};
      const int ready = ::poll(&descriptor, 1, 100);
      if (ready > 0 && (descriptor.revents & POLLIN) != 0) return 81;
    }
    if (mode == "hup_missing") {
      ::close(STDIN_FILENO);
      ::signal(SIGTERM, SIG_IGN);
      while (true) ::pause();
    }
    if (mode == "ignore_term") {
      ::signal(SIGTERM, SIG_IGN);
      while (true) ::pause();
    }
    if (mode == "ignore_term_bad") {
      ::signal(SIGTERM, SIG_IGN);
      static_cast<void>(step("999", 0));
      while (true) ::pause();
    }
    if (mode == "partial") {
      static_cast<void>(write_all(STDERR_FILENO, "FULL_ANALYZE_ST"));
      return 0;
    }
    if (mode == "stderr_bad") {
      static_cast<void>(write_repeated(STDERR_FILENO, 'e', 128U * 1024U));
      static_cast<void>(raw_step(
          "FULL_ANALYZE_STEP\t1\tEVENT\t999\tPLAYER\t0\tELAPSED_SECONDS\t0\tSTEP_SECONDS\t0\n"));
      ::signal(SIGTERM, SIG_IGN);
      while (true) ::pause();
    }
    if (mode == "wrong_eid") {
      static_cast<void>(step("999", 0));
    } else if (mode == "delayed_duplicate" && event_count == 2) {
      static_cast<void>(step(previous_id, 0));
    } else if (mode == "wrong_player") {
      static_cast<void>(step(id, 1));
    } else if (mode == "duplicate") {
      static_cast<void>(step(id, 1));
      static_cast<void>(step(id, 1));
    } else if (mode == "long_step") {
      static_cast<void>(write_all(STDERR_FILENO, "FULL_ANALYZE_STEP\t"));
      static_cast<void>(write_repeated(STDERR_FILENO, 'x', 8192));
      static_cast<void>(write_all(STDERR_FILENO, "\n"));
    } else if (mode == "missing_field") {
      static_cast<void>(
          raw_step("FULL_ANALYZE_STEP\t1\tEVENT\t0\tPLAYER\t0\tELAPSED_SECONDS\t0\n"));
    } else if (mode == "wrong_label") {
      static_cast<void>(
          raw_step("FULL_ANALYZE_STEP\t1\tEVENT\t0\tPLAYER\t0\tELAPSED\t0\tSTEP_SECONDS\t0\n"));
    } else if (mode == "extra_field") {
      static_cast<void>(raw_step(
          "FULL_ANALYZE_STEP\t1\tEVENT\t0\tPLAYER\t0\tELAPSED_SECONDS\t0\tSTEP_SECONDS\t0\tX\n"));
    } else if (mode == "nan_duration") {
      static_cast<void>(raw_step(
          "FULL_ANALYZE_STEP\t1\tEVENT\t0\tPLAYER\t0\tELAPSED_SECONDS\tnan\tSTEP_SECONDS\t0\n"));
    } else if (mode == "inf_duration") {
      static_cast<void>(raw_step(
          "FULL_ANALYZE_STEP\t1\tEVENT\t0\tPLAYER\t0\tELAPSED_SECONDS\t0\tSTEP_SECONDS\tinf\n"));
    } else if (mode == "zero_step_number") {
      static_cast<void>(raw_step(
          "FULL_ANALYZE_STEP\t0\tEVENT\t0\tPLAYER\t0\tELAPSED_SECONDS\t0\tSTEP_SECONDS\t0\n"));
    } else if (mode == "skipped_step") {
      static_cast<void>(raw_step(
          "FULL_ANALYZE_STEP\t2\tEVENT\t0\tPLAYER\t0\tELAPSED_SECONDS\t0\tSTEP_SECONDS\t0\n"));
    } else if (mode == "descendant") {
      const pid_t descendant = ::fork();
      if (descendant < 0) return 87;
      if (descendant == 0) {
        ::sleep(10);
        _exit(0);
      }
      const std::string notice = "DESCENDANT_PID\t" + std::to_string(descendant) + "\n";
      static_cast<void>(write_all(STDERR_FILENO, notice));
      return 0;
    } else if (mode == "large_no_newline") {
      if (!write_repeated(STDERR_FILENO, 'e', 2U * 1024U * 1024U)) return 83;
    } else if (mode == "large") {
      if (!write_repeated(STDOUT_FILENO, 'o', 2U * 1024U * 1024U)) return 82;
      if (!write_repeated(STDERR_FILENO, 'e', 2U * 1024U * 1024U)) return 83;
    } else if (mode == "output_256k") {
      if (!write_repeated(STDOUT_FILENO, 'o', 256U * 1024U)) return 82;
    } else if (mode.starts_with("mask")) {
      int mask = 0;
      const std::string_view text = mode.substr(4);
      const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), mask);
      if (error != std::errc{} || end != text.data() + text.size()) return 84;
      if (field(line, 2) == "DRAW") {
        if ((mask & 1) != 0) static_cast<void>(step(id, 0));
      } else {
        for (int player = 1; player < 4; ++player) {
          if ((mask & (1 << player)) != 0) static_cast<void>(step(id, player));
        }
      }
    } else if (name == "ACTION" && field(line, 2) == "DRAW") {
      static_cast<void>(step(id, 0));
    } else if (name == "ACTION" &&
               (field(line, 2) == "DISCARD" || field(line, 2) == "UPGRADED_KAN")) {
      int actor = -1;
      const std::string_view actor_text = field(line, 3);
      static_cast<void>(
          std::from_chars(actor_text.data(), actor_text.data() + actor_text.size(), actor));
      for (int player = 0; player < 4; ++player) {
        if (player != actor) static_cast<void>(step(id, player));
      }
    }
    std::string output;
    output.reserve(id.size() + 80);
    output.append("POS\t");
    output.append(id);
    output.append("\t0\t1\nCAND\t0\t0.3\t0.5\t0.2\t-0.1\t1\nACTION\t0\tPASS\t0\n");
    if (!write_all(STDOUT_FILENO, output)) return 85;
    if (mode == "zero_step" && event_count == 1) {
      if (!input.buffered()) {
        struct pollfd descriptor{STDIN_FILENO, POLLIN, 0};
        const int ready = ::poll(&descriptor, 1, 500);
        if (ready <= 0 || (descriptor.revents & POLLIN) == 0) return 86;
      }
    }
    previous_id = id;
  }
  if (mode == "finish_duplicate") static_cast<void>(step(previous_id, 0));
  return 0;
}
