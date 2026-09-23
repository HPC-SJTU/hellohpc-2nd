#include "maimoe/state.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <utility>

namespace maimoe {
namespace {

constexpr std::uint64_t kMixMultiplier = 0xd6e8feb86659fd93ULL;
constexpr std::uint64_t kMixIncrement = 0xa5a3564e27f8862fULL;

constexpr std::array<std::array<std::int16_t, 2>, kTouchSensorCount> kTouchPositions = {{
    {128, 8}, {173, 21}, {192, 72}, {173, 123}, {128, 136}, {83, 123}, {64, 72}, {83, 21},
    {128, 24}, {162, 38}, {176, 72}, {162, 106}, {128, 120}, {94, 106}, {80, 72}, {94, 38},
    {128, 72},
    {128, 41}, {150, 50}, {159, 72}, {150, 94}, {128, 103}, {106, 94}, {97, 72}, {106, 50},
    {128, 55}, {140, 60}, {145, 72}, {140, 84}, {128, 89}, {116, 84}, {111, 72}, {116, 60},
}};

void add_button_energy(State& state, std::uint8_t lane, std::uint32_t value) noexcept {
    if (lane >= state.energy.size()) {
        return;
    }
    state.energy[lane] = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(65535U, static_cast<std::uint32_t>(state.energy[lane]) + value));
}

void add_touch_energy(State& state, std::uint8_t sensor, std::uint32_t value) noexcept {
    if (sensor >= state.touch_energy.size()) {
        return;
    }
    state.touch_energy[sensor] = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(65535U,
            static_cast<std::uint32_t>(state.touch_energy[sensor]) + value));
}

void mix_event(State& state, const Event& event) noexcept {
    state.phase = std::rotl(state.phase ^ event_word(event.canonical), 13) * kMixMultiplier +
                  kMixIncrement;
}

void apply_event(State& state, const Event& event) noexcept {
    switch (event.type) {
    case EventType::Tap:
        if (event.lane < state.energy.size()) {
            add_button_energy(state, event.lane, event.strength);
            state.taps[event.lane] = LatestTap{true, event.strength};
        }
        break;
    case EventType::Hold:
        if (event.lane < state.energy.size()) {
            add_button_energy(state, event.lane,
                              std::max<std::uint32_t>(1U, event.strength / 4U));
            state.holds[event.lane] = ActiveHold{true, event.end_tick, event.strength};
        }
        break;
    case EventType::Slide:
        if (event.lane < state.energy.size() && event.target_lane < state.energy.size()) {
            const std::uint32_t distance = event.target_lane > event.lane
                                               ? event.target_lane - event.lane
                                               : event.lane - event.target_lane;
            add_button_energy(state, event.lane, 8U * (distance + 1U));
            state.slides[event.lane] = ActiveSlide{
                true, event.end_tick, event.target_lane, event.path_id};
        }
        break;
    case EventType::Break:
        if (event.lane < state.energy.size()) {
            add_button_energy(state, event.lane, 2U * event.strength);
            state.breaks[event.lane] = LatestBreak{true, event.strength, 3};
            state.break_remaining = 3;
        }
        break;
    case EventType::TouchTap:
        if (event.touch_sensor < state.touch_energy.size()) {
            add_touch_energy(state, event.touch_sensor, event.strength);
            state.touches[event.touch_sensor] = LatestTouch{true, false, event.strength};
        }
        break;
    case EventType::TouchHold:
        if (event.touch_sensor < state.touch_energy.size()) {
            add_touch_energy(state, event.touch_sensor,
                             std::max<std::uint32_t>(1U, event.strength / 4U));
            state.touch_holds[event.touch_sensor] = ActiveTouchHold{
                true, event.end_tick, event.strength};
            state.touches[event.touch_sensor] = LatestTouch{true, true, event.strength};
        }
        break;
    }
    mix_event(state, event);
}

template <typename Consumer>
void evolve_states_impl(std::uint64_t chart_id, const ParsedChart& chart,
                        Consumer&& consume) {
    State current;
    const std::uint64_t bpm_q16 =
        (static_cast<std::uint64_t>(chart.metadata.whole_bpm_milli) * 65536ULL + 500ULL) /
        1000ULL;
    current.phase = 0x6a09e667f3bcc909ULL ^ chart_id ^ (bpm_q16 << 17U) ^
                    static_cast<std::uint64_t>(kTouchSensorCount);
    current.warning_count = static_cast<std::uint32_t>(chart.warnings.size());
    for (const Warning& warning : chart.warnings) {
        current.latest_warning_severity = warning.severity;
    }

    std::size_t next_event = 0;
    while (next_event < chart.events.size() && chart.events[next_event].tick == 0U) {
        apply_event(current, chart.events[next_event]);
        ++next_event;
    }
    consume(0U, current);

    for (std::size_t frame = 0; frame + 1U < kFrameCount; ++frame) {
        State next = current;
        const std::uint32_t target_tick = frame_tick(chart.max_tick, frame + 1U);
        for (std::size_t lane = 0; lane < next.energy.size(); ++lane) {
            next.energy[lane] = static_cast<std::uint16_t>(
                (7U * static_cast<std::uint32_t>(next.energy[lane])) / 8U);
            if (current.holds[lane].present) {
                add_button_energy(next, static_cast<std::uint8_t>(lane),
                                  std::max<std::uint32_t>(
                                      1U, current.holds[lane].strength / 8U));
            }
            if (current.slides[lane].present) {
                const std::uint8_t target = current.slides[lane].target_lane;
                const std::uint32_t distance = target > lane
                                                   ? target - static_cast<std::uint32_t>(lane)
                                                   : static_cast<std::uint32_t>(lane) - target;
                add_button_energy(next, static_cast<std::uint8_t>(lane),
                                  4U * (distance + 1U));
            }
        }
        for (std::size_t sensor = 0; sensor < next.touch_energy.size(); ++sensor) {
            next.touch_energy[sensor] = static_cast<std::uint16_t>(
                (7U * static_cast<std::uint32_t>(next.touch_energy[sensor])) / 8U);
            if (current.touch_holds[sensor].present) {
                add_touch_energy(next, static_cast<std::uint8_t>(sensor),
                                 std::max<std::uint32_t>(
                                     1U, current.touch_holds[sensor].strength / 8U));
            }
        }
        if (next.break_remaining > 0U) {
            --next.break_remaining;
        }
        for (LatestBreak& note_break : next.breaks) {
            if (note_break.flash_frames > 0U) {
                --note_break.flash_frames;
            }
        }
        for (std::size_t lane = 0; lane < next.holds.size(); ++lane) {
            if (next.holds[lane].present && next.holds[lane].end_tick <= target_tick) {
                next.holds[lane] = ActiveHold{};
            }
            if (next.slides[lane].present && next.slides[lane].end_tick <= target_tick) {
                next.slides[lane] = ActiveSlide{};
            }
        }
        for (ActiveTouchHold& hold : next.touch_holds) {
            if (hold.present && hold.end_tick <= target_tick) {
                hold = ActiveTouchHold{};
            }
        }
        next.phase = std::rotl(next.phase ^ static_cast<std::uint64_t>(frame + 1U), 7) *
                         kMixMultiplier +
                     kMixIncrement;
        while (next_event < chart.events.size() && chart.events[next_event].tick <= target_tick) {
            apply_event(next, chart.events[next_event]);
            ++next_event;
        }
        current = std::move(next);
        consume(frame + 1U, current);
    }
}

}

std::array<std::int16_t, 2> touch_sensor_position(std::uint8_t sensor) {
    if (sensor >= kTouchPositions.size()) {
        throw std::out_of_range("touch sensor must be in 0..32");
    }
    return kTouchPositions[sensor];
}

std::uint32_t frame_tick(std::uint32_t max_tick, std::size_t frame) {
    if (frame >= kFrameCount) {
        throw std::out_of_range("frame must be in 0..9");
    }
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(max_tick) * frame) / 9U);
}

std::uint64_t event_word(std::string_view canonical_line) noexcept {
    std::uint64_t hash = 0x243f6a8885a308d3ULL;
    for (const unsigned char byte : canonical_line) {
        hash = std::rotl(hash ^ static_cast<std::uint64_t>(byte), 5) *
                   0x9e3779b185ebca87ULL +
               0x165667b19e3779f9ULL;
    }
    return hash ^ static_cast<std::uint64_t>(canonical_line.size());
}

StateSequence evolve_states(std::uint64_t chart_id, const ParsedChart& chart) {
    StateSequence sequence{};
    evolve_states_impl(chart_id, chart, [&](std::size_t frame, const State& state) {
        sequence[frame] = state;
    });
    return sequence;
}

EndpointStates evolve_endpoint_states(std::uint64_t chart_id, const ParsedChart& chart) {
    EndpointStates endpoints;
    evolve_states_impl(chart_id, chart, [&](std::size_t frame, const State& state) {
        if (frame == kFirstSampleFrame) {
            endpoints.frame_begin = state;
        } else if (frame == kLastSampleFrame) {
            endpoints.frame_end = state;
        }
    });
    return endpoints;
}

Bytes serialize_state(const State& state) {
    Bytes bytes(kSerializedStateBytes);
    serialize_state_into(
        state, std::span<std::uint8_t, kSerializedStateBytes>(bytes.data(), bytes.size()));
    return bytes;
}

void serialize_state_into(
    const State& state, std::span<std::uint8_t, kSerializedStateBytes> bytes) {
    std::size_t offset = 0;
    const auto append_byte = [&](std::uint8_t value) { bytes[offset++] = value; };
    const auto append_u16 = [&](std::uint16_t value) {
        for (std::size_t index = 0; index < sizeof(value); ++index) {
            append_byte(static_cast<std::uint8_t>(value & 0xffU));
            value >>= 8U;
        }
    };
    const auto append_u32 = [&](std::uint32_t value) {
        for (std::size_t index = 0; index < sizeof(value); ++index) {
            append_byte(static_cast<std::uint8_t>(value & 0xffU));
            value >>= 8U;
        }
    };
    const auto append_u64 = [&](std::uint64_t value) {
        for (std::size_t index = 0; index < sizeof(value); ++index) {
            append_byte(static_cast<std::uint8_t>(value & 0xffU));
            value >>= 8U;
        }
    };

    for (const std::uint8_t value : std::array<std::uint8_t, 8>{
             'M', 'M', 'S', '9', 1U, 0U, 0U, 0U}) {
        append_byte(value);
    }
    append_u32(static_cast<std::uint32_t>(state.speed_q16));
    append_u32(static_cast<std::uint32_t>(state.rotation_q16));
    append_byte(state.mirror);
    append_byte(state.alpha);
    append_byte(state.asset_slot);
    append_byte(state.asset_mode);
    for (const std::uint16_t energy : state.energy) {
        append_u16(energy);
    }
    for (const std::uint16_t energy : state.touch_energy) {
        append_u16(energy);
    }
    append_byte(state.break_remaining);
    for (const ActiveHold& hold : state.holds) {
        append_byte(hold.present ? 1U : 0U);
        append_u32(hold.present ? hold.end_tick : 0U);
        append_byte(hold.present ? hold.strength : 0U);
    }
    for (const ActiveSlide& slide : state.slides) {
        append_byte(slide.present ? 1U : 0U);
        append_u32(slide.present ? slide.end_tick : 0U);
        append_byte(slide.present ? slide.target_lane : 0U);
        append_byte(slide.present ? slide.path_id : 0U);
    }
    for (const LatestTap& tap : state.taps) {
        append_byte(tap.present ? 1U : 0U);
        append_byte(tap.present ? tap.strength : 0U);
    }
    for (const LatestBreak& note_break : state.breaks) {
        append_byte(note_break.present ? 1U : 0U);
        append_byte(note_break.present ? note_break.strength : 0U);
        append_byte(note_break.present ? note_break.flash_frames : 0U);
    }
    for (const ActiveTouchHold& hold : state.touch_holds) {
        append_byte(hold.present ? 1U : 0U);
        append_u32(hold.present ? hold.end_tick : 0U);
        append_byte(hold.present ? hold.strength : 0U);
    }
    for (const LatestTouch& touch : state.touches) {
        append_byte(touch.present ? 1U : 0U);
        append_byte(touch.present && touch.hold ? 1U : 0U);
        append_byte(touch.present ? touch.strength : 0U);
    }
    append_u32(state.warning_count);
    append_byte(state.latest_warning_severity);
    append_u64(state.phase);
    if (offset != bytes.size()) {
        throw FormatError("internal state serializer size mismatch");
    }
}

}
