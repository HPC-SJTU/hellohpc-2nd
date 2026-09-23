#pragma once

#include "maimoe/state.hpp"

#include <cstddef>
#include <cstdint>

namespace maimoe {

[[nodiscard]] Bytes render_rgba(std::uint64_t chart_id, const ParsedChart& chart,
                                const State& state, std::size_t frame);
[[nodiscard]] Bytes render_gray8(std::uint64_t chart_id, const ParsedChart& chart,
                                 const State& state, std::size_t frame);
[[nodiscard]] Bytes render_output_rgba(std::uint64_t chart_id, const ParsedChart& chart,
                                       const State& state, std::size_t frame);
void render_output_rgba_into(
    std::uint64_t chart_id, const ParsedChart& chart, const State& state, std::size_t frame,
    std::span<std::uint8_t, kOutputFramePayloadBytes> output);
[[nodiscard]] Bytes sample_output_frame(std::span<const std::uint8_t> rgba);

}
