#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace full_analyze_interactor {

struct Event {
  std::string text;
  std::string id;
  std::size_t first_line;
};

class EventReader {
 public:
  explicit EventReader(int input_fd) : input_fd_(input_fd) {}

  auto next() -> std::expected<std::optional<Event>, std::string>;

 private:
  auto physical_line() -> std::expected<std::optional<std::string>, std::string>;

  int input_fd_;
  std::string buffer_;
  std::size_t buffer_offset_ = 0;
  bool eof_ = false;
  std::size_t line_number_ = 0;
  unsigned long long next_event_id_ = 0;
};

struct RunOptions {
  int output_fd = 1;
  int error_fd = 2;
};

// Returns the child exit status. Interactor/input/protocol errors return 2.
auto run(int record_fd, int mask, std::span<const std::string_view> command,
         const RunOptions &options = {}) -> int;

}  // namespace full_analyze_interactor
