// Minimal exec target for submitted kernels.  This translation unit contains
// no task reference/oracle and links no harness implementation.
#include "common/isolated_runner.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

using Bitmatrix = void (*)(int, const uint64_t*, const uint64_t*, uint32_t*);
using Fft = void (*)(const float*, float*, int, int);


}  // namespace

int main(int argc, char** argv) {
  if (argc < 4 || std::strcmp(argv[1], "--cheatsheet-kernel-worker") != 0) return 104;
  const char* symbol = argv[3];
  if (std::strcmp(symbol, "kernel_bitmatrix") == 0) {
    return isolated::run_worker_invocation<Bitmatrix>(
        argc, argv, symbol, false,
        [](Bitmatrix fn, const std::vector<void*>& regions,
           const std::vector<std::string>& scalars) {
          if (regions.size() != 3 || scalars.size() != 1) _exit(105);
          fn(isolated::parse_int(scalars[0]),
             static_cast<const uint64_t*>(regions[0]),
             static_cast<const uint64_t*>(regions[1]),
             static_cast<uint32_t*>(regions[2]));
        });
  }
  if (std::strcmp(symbol, "kernel_fft") == 0) {
    return isolated::run_worker_invocation<Fft>(
        argc, argv, symbol, false,
        [](Fft fn, const std::vector<void*>& regions,
           const std::vector<std::string>& scalars) {
          if (regions.size() != 2 || scalars.size() != 2) _exit(105);
          fn(static_cast<const float*>(regions[0]),
             static_cast<float*>(regions[1]),
             isolated::parse_int(scalars[0]),
             isolated::parse_int(scalars[1]));
        });
  }
  return 104;
}
