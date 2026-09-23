#pragma once

#include "kernel_operator.h"

namespace RsmBaseline {

constexpr uint32_t kScoreTile = 4096;
constexpr uint32_t kMaxD = 256;
constexpr uint32_t kAlignment = 8;
constexpr uint32_t kRowsPerXTile = 8;

class ComputeCore {
public:
    __aicore__ inline void Init(
        GM_ADDR score, GM_ADDR x, GM_ADDR offsets, GM_ADDR mean, GM_ADDR rstd,
        GM_ADDR logsumexp, uint32_t n, uint32_t d, float epsilon)
    {
        d_ = d;
        epsilon_ = epsilon;
        score_gm_.SetGlobalBuffer(reinterpret_cast<__gm__ half *>(score), n);
        x_gm_.SetGlobalBuffer(reinterpret_cast<__gm__ half *>(x), n * d);
        offsets_gm_.SetGlobalBuffer(reinterpret_cast<__gm__ int32_t *>(offsets));
        mean_gm_.SetGlobalBuffer(reinterpret_cast<__gm__ float *>(mean));
        rstd_gm_.SetGlobalBuffer(reinterpret_cast<__gm__ float *>(rstd));
        logsumexp_gm_.SetGlobalBuffer(reinterpret_cast<__gm__ float *>(logsumexp));

        pipe_.InitBuffer(score_half_buffer_, kScoreTile * sizeof(half));
        pipe_.InitBuffer(score_float_buffer_, kScoreTile * sizeof(float));
        pipe_.InitBuffer(reduce_output_buffer_, 64 * sizeof(float));
        pipe_.InitBuffer(reduce_work_buffer_, kScoreTile * sizeof(float));
        pipe_.InitBuffer(x_half_buffer_, kRowsPerXTile * kMaxD * sizeof(half));
        pipe_.InitBuffer(x_float_buffer_, kRowsPerXTile * kMaxD * sizeof(float));
        pipe_.InitBuffer(mean_buffer_, kMaxD * sizeof(float));
        pipe_.InitBuffer(m2_buffer_, kMaxD * sizeof(float));
        pipe_.InitBuffer(mean_residual_buffer_, kMaxD * sizeof(float));
        pipe_.InitBuffer(variance_chunk_buffer_, kMaxD * sizeof(float));
        pipe_.InitBuffer(mean_correction_buffer_, kMaxD * sizeof(float));
        pipe_.InitBuffer(delta_buffer_, kMaxD * sizeof(float));
        pipe_.InitBuffer(temp_buffer_, kMaxD * sizeof(float));
    }

    __aicore__ inline void ProcessSegment(uint32_t segment)
    {
        const uint32_t begin = static_cast<uint32_t>(offsets_gm_.GetValue(segment));
        const uint32_t end = static_cast<uint32_t>(offsets_gm_.GetValue(segment + 1));
        const float maximum = SegmentMaximum(begin, end);
        const float normalizer = AccumulateMoments(begin, end, maximum);
        FinalizeMoments(normalizer);
        StoreResults(segment, maximum, normalizer);
    }

private:
    __aicore__ inline uint32_t Minimum(uint32_t lhs, uint32_t rhs) const
    {
        return lhs < rhs ? lhs : rhs;
    }

    __aicore__ inline void LoadScores(
        uint32_t begin, uint32_t count, float maximum, bool exponentiate)
    {
        auto score_half = score_half_buffer_.Get<half>();
        const uint16_t bytes = static_cast<uint16_t>(count * sizeof(half));
        AscendC::DataCopyPad(score_half, score_gm_[begin], {1, bytes, 0, 0}, {});
        AscendC::PipeBarrier<PIPE_ALL>();
        auto score_float = score_float_buffer_.Get<float>();
        AscendC::Cast(score_float, score_half, AscendC::RoundMode::CAST_NONE, count);
        if (exponentiate) {
            AscendC::Adds(score_float, score_float, -maximum, count);
            AscendC::Exp(score_float, score_float, count);
        }
        AscendC::PipeBarrier<PIPE_V>();
    }

    __aicore__ inline void LoadRows(uint32_t begin, uint32_t rows)
    {
        const uint32_t elements = rows * d_;
        const uint16_t bytes = static_cast<uint16_t>(elements * sizeof(half));
        auto x_half = x_half_buffer_.Get<half>();
        AscendC::DataCopyPad(x_half, x_gm_[begin * d_], {1, bytes, 0, 0}, {});
        AscendC::PipeBarrier<PIPE_ALL>();
        AscendC::Cast(
            x_float_buffer_.Get<float>(), x_half,
            AscendC::RoundMode::CAST_NONE, elements);
        AscendC::PipeBarrier<PIPE_V>();
    }

    __aicore__ inline float SegmentMaximum(uint32_t begin, uint32_t end)
    {
        auto score_float = score_float_buffer_.Get<float>();
        auto reduce_output = reduce_output_buffer_.Get<float>();
        auto reduce_work = reduce_work_buffer_.Get<float>();
        float maximum = -65504.0f;
        for (uint32_t base = begin; base < end; base += kScoreTile) {
            const uint32_t count = Minimum(kScoreTile, end - base);
            LoadScores(base, count, 0.0f, false);
            AscendC::ReduceMax(reduce_output, score_float, reduce_work, count, false);
            AscendC::PipeBarrier<PIPE_V>();
            const float tile_maximum = reduce_output.GetValue(0);
            maximum = maximum > tile_maximum ? maximum : tile_maximum;
        }
        return maximum;
    }

    // Sum exp(score-max) and exp(score-max)*x together, with scalar and
    // vector Kahan compensation. Normalize once after the segment. With
    // finite FP16 x and at most 2^20 rows, the unnormalized sum fits in FP32.
    // Refine the resulting mean before computing centered variance.
    __aicore__ inline float AccumulateMoments(
        uint32_t begin, uint32_t end, float maximum)
    {
        auto sum = mean_buffer_.Get<float>();
        auto correction = mean_correction_buffer_.Get<float>();
        auto term = temp_buffer_.Get<float>();
        auto delta = delta_buffer_.Get<float>();
        auto x_float = x_float_buffer_.Get<float>();
        auto score_float = score_float_buffer_.Get<float>();
        AscendC::Duplicate(sum, 0.0f, d_);
        AscendC::Duplicate(correction, 0.0f, d_);
        AscendC::PipeBarrier<PIPE_V>();

        float weight_total = 0.0f;
        float weight_correction = 0.0f;
        for (uint32_t base = begin; base < end; base += kScoreTile) {
            const uint32_t count = Minimum(kScoreTile, end - base);
            LoadScores(base, count, maximum, true);
            for (uint32_t row_base = 0; row_base < count; row_base += kRowsPerXTile) {
                const uint32_t rows = Minimum(kRowsPerXTile, count - row_base);
                LoadRows(base + row_base, rows);
                for (uint32_t row = 0; row < rows; ++row) {
                    const float weight = score_float.GetValue(row_base + row);
                    const float y = weight - weight_correction;
                    const float next = weight_total + y;
                    weight_correction = (next - weight_total) - y;
                    weight_total = next;

                    AscendC::Muls(term, x_float[row * d_], weight, d_);
                    AscendC::Sub(delta, term, correction, d_);
                    AscendC::Add(term, sum, delta, d_);
                    AscendC::Sub(correction, term, sum, d_);
                    AscendC::Sub(correction, correction, delta, d_);
                    AscendC::DataCopy(sum, term, d_);
                }
                AscendC::PipeBarrier<PIPE_V>();
            }
        }
        AscendC::PipeBarrier<PIPE_V>();
        AscendC::Muls(sum, sum, 1.0f / weight_total, d_);
        AscendC::PipeBarrier<PIPE_V>();
        RefineMean(begin, end, maximum, weight_total);
        AccumulateCenteredVariance(begin, end, maximum);
        return weight_total;
    }

    // Recenter around the preliminary mean. This keeps constant columns
    // exact and recovers sub-ULP updates before the final FP32 rounding,
    // without choosing an arbitrary input row as a potentially distant center.
    __aicore__ inline void RefineMean(
        uint32_t begin, uint32_t end, float maximum, float normalizer)
    {
        auto mean = mean_buffer_.Get<float>();
        auto residual = mean_residual_buffer_.Get<float>();
        auto correction = mean_correction_buffer_.Get<float>();
        auto term = temp_buffer_.Get<float>();
        auto delta = delta_buffer_.Get<float>();
        auto x_float = x_float_buffer_.Get<float>();
        auto score_float = score_float_buffer_.Get<float>();
        AscendC::Duplicate(residual, 0.0f, d_);
        AscendC::Duplicate(correction, 0.0f, d_);
        for (uint32_t base = begin; base < end; base += kScoreTile) {
            const uint32_t count = Minimum(kScoreTile, end - base);
            LoadScores(base, count, maximum, true);
            for (uint32_t row_base = 0; row_base < count; row_base += kRowsPerXTile) {
                const uint32_t rows = Minimum(kRowsPerXTile, count - row_base);
                LoadRows(base + row_base, rows);
                for (uint32_t row = 0; row < rows; ++row) {
                    AscendC::Sub(term, x_float[row * d_], mean, d_);
                    AscendC::Muls(term, term, score_float.GetValue(row_base + row), d_);
                    AscendC::Sub(delta, term, correction, d_);
                    AscendC::Add(term, residual, delta, d_);
                    AscendC::Sub(correction, term, residual, d_);
                    AscendC::Sub(correction, correction, delta, d_);
                    AscendC::DataCopy(residual, term, d_);
                }
                AscendC::PipeBarrier<PIPE_V>();
            }
        }
        AscendC::Muls(residual, residual, 1.0f / normalizer, d_);
        AscendC::Add(mean, mean, residual, d_);
        AscendC::PipeBarrier<PIPE_V>();
    }

    __aicore__ inline void AccumulateCenteredVariance(
        uint32_t begin, uint32_t end, float maximum)
    {
        auto mean = mean_buffer_.Get<float>();
        auto m2 = m2_buffer_.Get<float>();
        auto temp = temp_buffer_.Get<float>();
        auto chunk = variance_chunk_buffer_.Get<float>();
        auto x_float = x_float_buffer_.Get<float>();
        auto score_float = score_float_buffer_.Get<float>();
        AscendC::Duplicate(m2, 0.0f, d_);
        for (uint32_t base = begin; base < end; base += kScoreTile) {
            const uint32_t count = Minimum(kScoreTile, end - base);
            LoadScores(base, count, maximum, true);
            for (uint32_t row_base = 0; row_base < count; row_base += kRowsPerXTile) {
                const uint32_t rows = Minimum(kRowsPerXTile, count - row_base);
                LoadRows(base + row_base, rows);
                AscendC::Duplicate(chunk, 0.0f, d_);
                for (uint32_t row = 0; row < rows; ++row) {
                    AscendC::Sub(temp, x_float[row * d_], mean, d_);
                    AscendC::Mul(temp, temp, temp, d_);
                    AscendC::Muls(temp, temp, score_float.GetValue(row_base + row), d_);
                    AscendC::Add(chunk, chunk, temp, d_);
                }
                AscendC::Add(m2, m2, chunk, d_);
                AscendC::PipeBarrier<PIPE_V>();
            }
        }
    }

    __aicore__ inline void FinalizeMoments(float normalizer)
    {
        auto m2 = m2_buffer_.Get<float>();
        AscendC::Muls(m2, m2, 1.0f / normalizer, d_);
        AscendC::Maxs(m2, m2, 0.0f, d_);
        AscendC::Adds(m2, m2, epsilon_, d_);
        AscendC::Rsqrt(m2, m2, d_);
        AscendC::PipeBarrier<PIPE_ALL>();
    }

    __aicore__ inline void StoreResults(
        uint32_t segment, float maximum, float normalizer)
    {
        auto mean = mean_buffer_.Get<float>();
        auto m2 = m2_buffer_.Get<float>();
        auto temp = temp_buffer_.Get<float>();
        const uint16_t bytes = static_cast<uint16_t>(d_ * sizeof(float));
        AscendC::DataCopyPad(mean_gm_[segment * d_], mean, {1, bytes, 0, 0});
        AscendC::DataCopyPad(rstd_gm_[segment * d_], m2, {1, bytes, 0, 0});

        auto scalar = reduce_output_buffer_.Get<float>();
        AscendC::Duplicate(scalar, normalizer, kAlignment);
        AscendC::Ln(scalar, scalar, kAlignment);
        AscendC::PipeBarrier<PIPE_V>();
        temp.SetValue(0, scalar.GetValue(0) + maximum);
        AscendC::PipeBarrier<PIPE_ALL>();
        AscendC::DataCopyPad(
            logsumexp_gm_[segment], temp, {1, sizeof(float), 0, 0});
        AscendC::PipeBarrier<PIPE_ALL>();
    }

    AscendC::TPipe pipe_;
    AscendC::TBuf<AscendC::TPosition::VECCALC> score_half_buffer_;
    AscendC::TBuf<AscendC::TPosition::VECCALC> score_float_buffer_;
    AscendC::TBuf<AscendC::TPosition::VECCALC> reduce_output_buffer_;
    AscendC::TBuf<AscendC::TPosition::VECCALC> reduce_work_buffer_;
    AscendC::TBuf<AscendC::TPosition::VECCALC> x_half_buffer_;
    AscendC::TBuf<AscendC::TPosition::VECCALC> x_float_buffer_;
    AscendC::TBuf<AscendC::TPosition::VECCALC> mean_buffer_;
    AscendC::TBuf<AscendC::TPosition::VECCALC> m2_buffer_;
    AscendC::TBuf<AscendC::TPosition::VECCALC> mean_residual_buffer_;
    AscendC::TBuf<AscendC::TPosition::VECCALC> variance_chunk_buffer_;
    AscendC::TBuf<AscendC::TPosition::VECCALC> mean_correction_buffer_;
    AscendC::TBuf<AscendC::TPosition::VECCALC> delta_buffer_;
    AscendC::TBuf<AscendC::TPosition::VECCALC> temp_buffer_;
    AscendC::GlobalTensor<half> score_gm_;
    AscendC::GlobalTensor<half> x_gm_;
    AscendC::GlobalTensor<int32_t> offsets_gm_;
    AscendC::GlobalTensor<float> mean_gm_;
    AscendC::GlobalTensor<float> rstd_gm_;
    AscendC::GlobalTensor<float> logsumexp_gm_;
    uint32_t d_ = 0;
    float epsilon_ = 0.0f;
};

}  // namespace RsmBaseline
