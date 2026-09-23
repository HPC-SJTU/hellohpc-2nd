# Fixed Baseline

The baseline assigns segments in a block-strided schedule across at most 40
vector cores. For each segment, it computes the numerically stable Softmax
maximum, then uses compensated vector accumulators to collect the normalized
weighted sum and weighted feature sum in a single pass. It refines the FP32
mean with a residual pass and computes the centered variance around that final
mean in a second pass over the input.

This implementation serves as the fixed numerical and performance reference.
It uses straightforward segment scheduling and does not perform shape-specific
dispatch or performance specialization. The current precision contract is
`rsm-math-fp64-conditioned-v2`; correctness is evaluated by the official public
and private test suites.
