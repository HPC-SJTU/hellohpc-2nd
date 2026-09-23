#pragma once

#include "maimoe/kernel_api.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace maimoe::worker {

inline constexpr std::uint64_t kRequestMagic = 0x315145524f4d4dULL;
inline constexpr std::uint64_t kResponseMagic = 0x315053524f4d4dULL;
inline constexpr std::size_t kHeaderBytes = 24U;

inline void store_u64(std::span<std::uint8_t> output, std::size_t offset,
                      std::uint64_t value) noexcept {
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        output[offset + index] = static_cast<std::uint8_t>(value & 0xffU);
        value >>= 8U;
    }
}

[[nodiscard]] inline std::uint64_t load_u64(std::span<const std::uint8_t> input,
                                             std::size_t offset) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        value |= static_cast<std::uint64_t>(input[offset + index]) << (8U * index);
    }
    return value;
}

[[nodiscard]] inline std::array<std::uint8_t, kHeaderBytes> request_header(
    std::uint64_t chart_id, std::uint64_t text_bytes) noexcept {
    std::array<std::uint8_t, kHeaderBytes> header{};
    store_u64(header, 0U, kRequestMagic);
    store_u64(header, 8U, chart_id);
    store_u64(header, 16U, text_bytes);
    return header;
}

[[nodiscard]] inline std::array<std::uint8_t, kHeaderBytes> response_header(
    std::uint64_t chart_id, std::uint64_t status) noexcept {
    std::array<std::uint8_t, kHeaderBytes> header{};
    store_u64(header, 0U, kResponseMagic);
    store_u64(header, 8U, chart_id);
    store_u64(header, 16U, status);
    return header;
}

}
