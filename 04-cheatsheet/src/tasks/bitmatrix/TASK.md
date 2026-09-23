# Task: Masked Bitmatrix Density

## Interface

Count enabled-row set bits by column:

```c
extern "C" void kernel_bitmatrix(
    int N,
    const uint64_t* matrix,
    const uint64_t* mask,
    uint32_t* result);
```

The matrix is an `N x N` bit matrix stored row-major in 64-bit words.
`N` is a multiple of 64 and `64 <= N <= 8192`. Row `i` starts at
`matrix + i * (N / 64)`; bit `j % 64` of word `j / 64` represents
column `j`. Bit `i % 64` of `mask[i / 64]` selects row `i`.

The buffers contain `N * N / 64`, `N / 64`, and `N` elements,
respectively, and are pairwise non-overlapping. `matrix` and `mask` are
immutable. The initial contents of `result` are unspecified.

## Correctness

For every column `j`, write the number of selected rows whose bit `j` is
set. Every `result` element must be overwritten. Results are checked exactly.

## Execution rules

The evaluator runs this task on one CPU thread. The submitted kernel must not
create threads or processes. C++ standard-library facilities, compiler builtins,
and target intrinsics are allowed. External libraries that perform this
bit-matrix operation are forbidden.

Minimize `median_time_ms` after correctness passes.
