#include "kernel.h"

#include <cmath>
#include <cstdint>

extern "C" void kernel_fft_reference(const float* input, float* output,
                                     int batch, int length) {
  constexpr double kPi = 3.14159265358979323846264338327950288;
  int log_n = 0;
  while ((1 << log_n) < length) ++log_n;
  for (int b = 0; b < batch; ++b) {
    const float* src = input + static_cast<std::size_t>(2) * b * length;
    float* dst = output + static_cast<std::size_t>(2) * b * length;
    for (int i = 0; i < length; ++i) {
      uint32_t x = static_cast<uint32_t>(i), rev = 0;
      for (int bit = 0; bit < log_n; ++bit) {
        rev = (rev << 1) | (x & 1u);
        x >>= 1;
      }
      dst[2 * rev] = src[2 * i];
      dst[2 * rev + 1] = src[2 * i + 1];
    }
    for (int span = 2; span <= length; span <<= 1) {
      const int half = span >> 1;
      for (int base = 0; base < length; base += span) {
        for (int j = 0; j < half; ++j) {
          const double angle = -2.0 * kPi * j / span;
          const double wr = std::cos(angle), wi = std::sin(angle);
          const int even = 2 * (base + j);
          const int odd = 2 * (base + j + half);
          const double er = dst[even], ei = dst[even + 1];
          const double or_ = dst[odd], oi = dst[odd + 1];
          const double tr = wr * or_ - wi * oi;
          const double ti = wr * oi + wi * or_;
          dst[even] = static_cast<float>(er + tr);
          dst[even + 1] = static_cast<float>(ei + ti);
          dst[odd] = static_cast<float>(er - tr);
          dst[odd + 1] = static_cast<float>(ei - ti);
        }
      }
    }
  }
}
