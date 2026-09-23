#pragma once

#include "maimoe/types.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace maimoe {

struct ActiveHold {
    bool present = false;
    std::uint32_t end_tick = 0;
    std::uint8_t strength = 0;
};

struct ActiveSlide {
    bool present = false;
    std::uint32_t end_tick = 0;
    std::uint8_t target_lane = 0;
    std::uint8_t path_id = 0;
};

struct LatestTap {
    bool present = false;
    std::uint8_t strength = 0;
};

struct LatestBreak {
    bool present = false;
    std::uint8_t strength = 0;
    std::uint8_t flash_frames = 0;
};

struct ActiveTouchHold {
    bool present = false;
    std::uint32_t end_tick = 0;
    std::uint8_t strength = 0;
};

struct LatestTouch {
    bool present = false;
    bool hold = false;
    std::uint8_t strength = 0;
};

struct State {
    std::int32_t speed_q16 = 65536;
    std::int32_t rotation_q16 = 0;
    std::uint8_t mirror = 0;
    std::uint8_t alpha = 255;
    std::uint8_t asset_slot = 255;
    std::uint8_t asset_mode = 0;
    std::array<std::uint16_t, 8> energy{};
    std::array<std::uint16_t, kTouchSensorCount> touch_energy{};
    std::uint8_t break_remaining = 0;
    std::array<ActiveHold, 8> holds{};
    std::array<ActiveSlide, 8> slides{};
    std::array<LatestTap, 8> taps{};
    std::array<LatestBreak, 8> breaks{};
    std::array<ActiveTouchHold, kTouchSensorCount> touch_holds{};
    std::array<LatestTouch, kTouchSensorCount> touches{};
    std::uint32_t warning_count = 0;
    std::uint8_t latest_warning_severity = 0;
    std::uint64_t phase = 0;
};

using StateSequence = std::array<State, kFrameCount>;

struct EndpointStates {
    State frame_begin;
    State frame_end;
};

inline constexpr std::size_t kSerializedStateBytes = 557U;

[[nodiscard]] std::uint32_t frame_tick(std::uint32_t max_tick, std::size_t frame);
[[nodiscard]] std::uint64_t event_word(std::string_view canonical_line) noexcept;
[[nodiscard]] StateSequence evolve_states(std::uint64_t chart_id, const ParsedChart& chart);
[[nodiscard]] EndpointStates evolve_endpoint_states(std::uint64_t chart_id,
                                                     const ParsedChart& chart);
[[nodiscard]] Bytes serialize_state(const State& state);
void serialize_state_into(
    const State& state, std::span<std::uint8_t, kSerializedStateBytes> output);
[[nodiscard]] std::array<std::int16_t, 2> touch_sensor_position(std::uint8_t sensor);

}
