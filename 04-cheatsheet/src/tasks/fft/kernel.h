#pragma once

extern "C" void kernel_fft(const float* input, float* output,
                           int batch, int length);
extern "C" void kernel_fft_reference(const float* input, float* output,
                                     int batch, int length);
