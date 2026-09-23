#include "kernel.h"

#include <cmath>
#include <cstdint>

extern "C" void kernel_fft(const float* input, float* output,
                           int batch, int length) {
  constexpr float kPi = 3.14159265358979323846f;
  int log_n = 0;
  while ((1 << log_n) < length) ++log_n;
  for (int b = 0; b < batch; ++b) {
    const float* src = input + static_cast<std::size_t>(2) * b * length;
    float* dst = output + static_cast<std::size_t>(2) * b * length;
    for (int i = 0; i < length; ++i) {
      uint32_t x = static_cast<uint32_t>(i);
      uint32_t rev = 0;
      for (int bit = 0; bit < log_n; ++bit) {
        rev = (rev << 1) | (x & 1u);
        x >>= 1;
      }
      dst[2 * rev] = src[2 * i];
      dst[2 * rev + 1] = src[2 * i + 1];
    }
    for (int span = 2; span <= length; span <<= 1) {
      const int half = span >> 1;
      const float angle = -2.0f * kPi / span;
      const float step_r = std::cos(angle);
      const float step_i = std::sin(angle);
      for (int base = 0; base < length; base += span) {
        float wr = 1.0f, wi = 0.0f;
        for (int j = 0; j < half; ++j) {
          const int even = 2 * (base + j);
          const int odd = 2 * (base + j + half);
          const float or_ = dst[odd];
          const float oi = dst[odd + 1];
          const float tr = wr * or_ - wi * oi;
          const float ti = wr * oi + wi * or_;
          const float er = dst[even];
          const float ei = dst[even + 1];
          dst[even] = er + tr;
          dst[even + 1] = ei + ti;
          dst[odd] = er - tr;
          dst[odd + 1] = ei - ti;
          const float next_wr = wr * step_r - wi * step_i;
          wi = wr * step_i + wi * step_r;
          wr = next_wr;
        }
      }
    }
  }
}
