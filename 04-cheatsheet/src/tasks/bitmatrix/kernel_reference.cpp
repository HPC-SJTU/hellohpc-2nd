#include "kernel.h"
#include <cstddef>

extern "C" void kernel_bitmatrix_reference(int N, const uint64_t* matrix,
                                           const uint64_t* mask,
                                           uint32_t* result) {
  const int words_per_row = N / 64;
  for (int col = 0; col < N; ++col) {
    const int word = col / 64;
    const int bit = col % 64;
    uint32_t count = 0;
    for (int row = 0; row < N; ++row) {
      if ((mask[row / 64] >> (row % 64)) & 1ULL) {
        count += static_cast<uint32_t>(
            (matrix[static_cast<std::size_t>(row) * words_per_row + word] >> bit) & 1ULL);
      }
    }
    result[col] = count;
  }
}
