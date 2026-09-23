#pragma once

#include "maimoe/kernel_api.hpp"
#include "maimoe/types.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace maimoe::worker {

inline constexpr std::uint32_t kSharedIdle = 0U;
inline constexpr std::uint32_t kSharedRequest = 1U;
inline constexpr std::uint32_t kSharedResponse = 2U;
inline constexpr std::uint32_t kSharedStop = 3U;

struct SharedBlock {
    std::uint32_t state = kSharedIdle;
    std::uint32_t reserved = 0U;
    std::uint64_t request_magic = 0U;
    std::uint64_t request_chart_id = 0U;
    std::uint64_t request_text_bytes = 0U;
    std::uint64_t response_magic = 0U;
    std::uint64_t response_chart_id = 0U;
    std::uint64_t response_status = 0U;
    std::array<char, kMaxChartBytes> text{};
    std::array<std::uint8_t, kernel::kEncodedChartBytes> output{};
};

static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free);
static_assert(alignof(SharedBlock) >= std::atomic_ref<std::uint32_t>::required_alignment);

#if defined(__linux__)
void initialize_shared_block();
[[nodiscard]] SharedBlock& shared_block() noexcept;
void wait_shared_state(std::uint32_t expected) noexcept;
void wake_shared_state() noexcept;
#endif

}
