#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace maimoe::kernel {

inline constexpr std::size_t kEncodedChartBytes = 18720U;

void process_chart(
    std::uint64_t chart_id, std::string_view chart_text,
    std::span<std::uint8_t, kEncodedChartBytes> output);

}
