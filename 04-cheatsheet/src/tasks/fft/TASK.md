# Task: Batched Complex FFT

## Interface

Compute an out-of-place, unnormalized forward complex FFT for every batch:

```c
extern "C" void kernel_fft(const float* input,
                           float* output,
                           int batch,
                           int length);
```

`batch > 0`. `length` is a power of two in `[16, 8192]`. Complex values
are interleaved fp32 pairs `[real, imag]`, and batches are contiguous. Each
buffer contains `2 * batch * length` floats. The buffers do not overlap,
`input` is immutable, and every output element must be overwritten.

## Correctness

For each batch `b` and frequency `k`, compute

```text
output[b, k] = sum_n(input[b, n] * exp(-2 * pi * i * k * n / length))
```

There is no normalization factor. Results are checked against the reference with
absolute tolerance `1e-2` and relative tolerance `1e-3`.

## Execution rules

The evaluator runs this task on one CPU thread. The submitted kernel must not
create threads or processes and must not allocate memory at runtime. C++
standard-library math facilities that do not allocate, compiler builtins, and
target intrinsics are allowed. External FFT libraries are forbidden.

Minimize `median_time_ms` after correctness passes.
