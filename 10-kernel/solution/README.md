# Author Solution

The author implementation uses contiguous transfers of `x`, centered FP32
moment accumulation, the native `Rsqrt` operation, and score normalizers shared
with `logsumexp`. Multiple shape-driven TilingKeys support runtime-balanced
complete segments, width-specialized short groups, low-`S` row splits,
power-law partial tasks, and saturated medium-length workloads. The
implementation reads segment lengths on the device, although the formal Host
interface also permits generic planning from `offsets`.

For short segments, the implementation uses compensated normalized weighted
sums, refines the mean with a compensated residual pass, and then computes the
variance around the final FP32 mean. This prevents small terms from being lost
when opposite-signed FP16 extrema cancel. The generic direct path applies the
same stable recomputation when the mean or shift exceeds 32 standard deviations,
where moment subtraction becomes ill-conditioned. Partial merges apply the
same scale check and stable recomputation. Compact balanced kernels reserve an
additional 1 KiB of local scratch; the global workspace size is unchanged.
The current reference evaluates all mathematics in FP64 and rounds only final
outputs under precision contract v2. Variance uses the final FP32 mean;
acceptance is determined by the official v2 tests rather than by reproducing
the reference rounding order.

Public timing measurements are diagnostic only and do not constitute a formal
score that includes hidden cases. Run the public workflow from the repository
root after each change and use the reported performance value for local
comparison.
