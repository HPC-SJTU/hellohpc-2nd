#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <cstddef>
#include <string_view>
#include <system_error>
#include <vector>

#include "interactor.hpp"

namespace {

auto write_error(std::string_view message) -> void {
  while (!message.empty()) {
    const ssize_t count = ::write(STDERR_FILENO, message.data(), message.size());
    if (count > 0) {
      message.remove_prefix(static_cast<std::size_t>(count));
    } else if (count < 0 && errno == EINTR) {
      continue;
    } else {
      return;
    }
  }
}

}  // namespace

auto main(int argc, char *argv[]) -> int {
  int separator = -1;
  for (int index = 3; index < argc; ++index) {
    if (std::string_view(argv[index]) == "--") {
      separator = index;
      break;
    }
  }
  if (separator != 3 || separator + 1 >= argc) {
    write_error("usage: interactor <game-record> <mask> -- <submission-command> [args...]\n");
    return 2;
  }

  const int record = ::open(argv[1], O_RDONLY | O_CLOEXEC);
  if (record < 0) {
    write_error("game-record: cannot open the file\n");
    return 2;
  }
  int mask = 0;
  const std::string_view mask_text(argv[2]);
  const auto [mask_end, mask_error] =
      std::from_chars(mask_text.data(), mask_text.data() + mask_text.size(), mask);
  if (mask_error != std::errc{} || mask_end != mask_text.data() + mask_text.size() || mask < 1 ||
      mask > 15) {
    write_error("mask: expected an integer in [1, 15]\n");
    ::close(record);
    return 2;
  }

  std::vector<std::string_view> command;
  command.reserve(static_cast<std::size_t>(argc - separator - 1));
  for (int index = separator + 1; index < argc; ++index) command.emplace_back(argv[index]);
  const int status = full_analyze_interactor::run(record, mask, command);
  ::close(record);
  return status;
}
