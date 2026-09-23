#include "maimoe/render.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace maimoe {
namespace {

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

struct Surface {
    std::span<std::uint8_t> pixels;
    int width = 0;
    int height = 0;
    bool sampled = false;
};

[[nodiscard]] Color gray(std::uint32_t value) noexcept {
    const auto byte = static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, value));
    return Color{byte, byte, byte, 255};
}

[[nodiscard]] std::size_t pixel_offset(const Surface& surface, int x, int y) noexcept {
    return (static_cast<std::size_t>(y) * static_cast<std::size_t>(surface.width) +
            static_cast<std::size_t>(x)) * 4U;
}

void blend_at(Surface& surface, int x, int y, Color source,
              std::uint8_t state_alpha) noexcept {
    if (x < 0 || x >= surface.width || y < 0 || y >= surface.height) {
        return;
    }
    const std::uint32_t alpha =
        (static_cast<std::uint32_t>(source.a) * state_alpha + 127U) / 255U;
    const std::size_t offset = pixel_offset(surface, x, y);
    const std::array<std::uint8_t, 3> source_components{source.r, source.g, source.b};
    for (std::size_t component = 0; component < source_components.size(); ++component) {
        const std::uint32_t destination = surface.pixels[offset + component];
        const std::uint32_t value =
            (static_cast<std::uint32_t>(source_components[component]) * alpha +
             destination * (255U - alpha) + 127U) /
            255U;
        surface.pixels[offset + component] = static_cast<std::uint8_t>(value);
    }
    surface.pixels[offset + 3U] = 255U;
}

void blend(Surface& surface, int x, int y, Color source,
           std::uint8_t state_alpha) noexcept {
    if (x < 0 || x >= kCanvasWidth || y < 0 || y >= kCanvasHeight) {
        return;
    }
    if (!surface.sampled) {
        blend_at(surface, x, y, source, state_alpha);
        return;
    }
    if ((x & 3) != 2 || (y & 3) != 2) {
        return;
    }
    blend_at(surface, (x - 2) / 4, (y - 2) / 4, source, state_alpha);
}

void rectangle(Surface& surface, int x0, int y0, int x1, int y1, Color color,
                std::uint8_t alpha) noexcept {
    x0 = std::max(x0, 0);
    y0 = std::max(y0, 0);
    x1 = std::min(x1, static_cast<int>(kCanvasWidth) - 1);
    y1 = std::min(y1, static_cast<int>(kCanvasHeight) - 1);
    if (x0 > x1 || y0 > y1) {
        return;
    }
    if (!surface.sampled) {
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                blend_at(surface, x, y, color, alpha);
            }
        }
        return;
    }
    const int first_x = (x0 + 1) / 4;
    const int first_y = (y0 + 1) / 4;
    for (int y = first_y; y < surface.height && 4 * y + 2 <= y1; ++y) {
        for (int x = first_x; x < surface.width && 4 * x + 2 <= x1; ++x) {
            blend_at(surface, x, y, color, alpha);
        }
    }
}

[[nodiscard]] int lane_x(const State& state, std::size_t lane) noexcept {
    const std::size_t logical = state.mirror != 0U ? 7U - lane : lane;
    const int base = static_cast<int>(((2U * logical + 1U) * kCanvasWidth) / 16U);
    const std::int64_t product = static_cast<std::int64_t>(state.rotation_q16) * kCanvasWidth;
    const int shift = static_cast<int>(product / 65536);
    int result = (base + shift) % static_cast<int>(kCanvasWidth);
    if (result < 0) {
        result += kCanvasWidth;
    }
    return result;
}

[[nodiscard]] std::uint16_t visible_energy(const State& state, std::size_t lane) noexcept {
    const std::int64_t scaled = static_cast<std::int64_t>(state.energy[lane]) * state.speed_q16;
    const std::int64_t value = scaled / 65536;
    return static_cast<std::uint16_t>(std::clamp<std::int64_t>(value, 0, 65535));
}

[[nodiscard]] std::uint16_t visible_touch_energy(const State& state,
                                                 std::size_t sensor) noexcept {
    const std::int64_t scaled =
        static_cast<std::int64_t>(state.touch_energy[sensor]) * state.speed_q16;
    const std::int64_t value = scaled / 65536;
    return static_cast<std::uint16_t>(std::clamp<std::int64_t>(value, 0, 65535));
}

void draw_slide_point(Surface& surface, int center_x, int center_y, Color color,
                       std::uint8_t alpha) noexcept {
    rectangle(surface, center_x - 1, center_y - 1, center_x + 1, center_y + 1,
              color, alpha);
}

void render_impl(std::uint64_t chart_id, const ParsedChart& chart, const State& state,
                 std::size_t frame, bool sampled, std::span<std::uint8_t> output) {
    if (frame >= kFrameCount) {
        throw std::out_of_range("frame must be in 0..9");
    }
    Surface surface;
    surface.width = sampled ? kOutputFrameWidth : kCanvasWidth;
    surface.height = sampled ? kOutputFrameHeight : kCanvasHeight;
    surface.sampled = sampled;
    const std::size_t expected_bytes = static_cast<std::size_t>(surface.width) *
        static_cast<std::size_t>(surface.height) * 4U;
    if (output.size() != expected_bytes) {
        throw std::invalid_argument("render target has the wrong size");
    }
    surface.pixels = output;
    std::span<std::uint8_t>& pixels = surface.pixels;
    const std::uint32_t c0 = static_cast<std::uint32_t>(chart_id & 0xffU);
    const std::uint32_t c1 = static_cast<std::uint32_t>((chart_id >> 8U) & 0xffU);
    const std::uint32_t c2 = static_cast<std::uint32_t>((chart_id >> 16U) & 0xffU);

    for (int output_y = 0; output_y < surface.height; ++output_y) {
        const int y = sampled ? 4 * output_y + 2 : output_y;
        for (int output_x = 0; output_x < surface.width; ++output_x) {
            const int x = sampled ? 4 * output_x + 2 : output_x;
            Color color;
            if ((chart_id & 7U) != 0U) {
                const std::uint32_t q = (13U * static_cast<std::uint32_t>(x / 16) +
                                         29U * static_cast<std::uint32_t>(y / 8) +
                                         7U * static_cast<std::uint32_t>(frame) + c0) &
                                        0xffU;
                color = Color{static_cast<std::uint8_t>(q),
                              static_cast<std::uint8_t>((3U * q + 17U + c1) & 0xffU),
                              static_cast<std::uint8_t>((5U * q + 29U + c2) & 0xffU), 255};
            } else {
                color = Color{
                    static_cast<std::uint8_t>((17U * static_cast<std::uint32_t>(x) +
                                               31U * static_cast<std::uint32_t>(y) +
                                               7U * static_cast<std::uint32_t>(frame) + c0) & 0xffU),
                    static_cast<std::uint8_t>((29U * static_cast<std::uint32_t>(x) +
                                               11U * static_cast<std::uint32_t>(y) +
                                               13U * static_cast<std::uint32_t>(frame) + c1) & 0xffU),
                    static_cast<std::uint8_t>((7U * static_cast<std::uint32_t>(x) +
                                               19U * static_cast<std::uint32_t>(y) +
                                               3U * static_cast<std::uint32_t>(frame) + c2) & 0xffU),
                    255};
            }
            const std::size_t offset = pixel_offset(surface, output_x, output_y);
            pixels[offset] = color.r;
            pixels[offset + 1U] = color.g;
            pixels[offset + 2U] = color.b;
            pixels[offset + 3U] = 255U;
        }
    }

    for (std::size_t lane = 0; lane < 8U; ++lane) {
        rectangle(surface, lane_x(state, lane), 0, lane_x(state, lane), kCanvasHeight - 1,
                  gray(48), state.alpha);
    }
    for (std::size_t lane = 0; lane < 8U; ++lane) {
        const std::uint16_t energy = visible_energy(state, lane);
        const int height = static_cast<int>((static_cast<std::uint32_t>(energy) * kCanvasHeight) / 65535U);
        if (height > 0) {
            const int x = lane_x(state, lane);
            rectangle(surface, x - 2, kCanvasHeight - height, x + 2, kCanvasHeight - 1,
                      gray(64U + (energy >> 10U)), state.alpha);
        }
    }
    const std::uint32_t tick = frame_tick(chart.max_tick, frame);
    for (std::size_t lane = 0; lane < 8U; ++lane) {
        const ActiveHold& hold = state.holds[lane];
        if (hold.present && hold.end_tick > tick) {
            const std::uint32_t remaining = hold.end_tick - tick;
            const std::uint32_t height = 1U +
                static_cast<std::uint32_t>((static_cast<std::uint64_t>(remaining) * 143U) /
                                           std::max<std::uint32_t>(1U, chart.max_tick));
            const int x = lane_x(state, lane);
            rectangle(surface, x - 1, 143 - static_cast<int>(std::min<std::uint32_t>(143U, height)),
                      x + 1, 143, gray(160U + (hold.strength >> 3U)), state.alpha);
        }
    }
    for (std::size_t lane = 0; lane < 8U; ++lane) {
        const ActiveSlide& slide = state.slides[lane];
        if (!slide.present || slide.target_lane >= 8U) {
            continue;
        }
        const int start_x = lane_x(state, lane);
        const int target_x = lane_x(state, slide.target_lane);
        for (int j = 0; j <= 31; ++j) {
            const int x = start_x + ((target_x - start_x) * j) / 31;
            const int y = 143 - (j * 143) / 31;
            draw_slide_point(surface, x, y, gray(176U + slide.path_id * 4U), state.alpha);
        }
    }
    for (std::size_t sensor = 0; sensor < kTouchSensorCount; ++sensor) {
        const auto position = touch_sensor_position(static_cast<std::uint8_t>(sensor));
        const int x = position[0];
        const int y = position[1];
        const std::uint16_t energy = visible_touch_energy(state, sensor);
        if (energy > 0U) {
            const int radius = 1 + static_cast<int>(energy >> 14U);
            rectangle(surface, x - radius, y - radius, x + radius, y + radius,
                      Color{64U, 208U, 224U,
                            static_cast<std::uint8_t>(128U + (energy >> 9U))},
                      state.alpha);
        }
        const ActiveTouchHold& hold = state.touch_holds[sensor];
        if (hold.present && hold.end_tick > tick) {
            const std::uint32_t remaining = hold.end_tick - tick;
            const int arm = 2 + static_cast<int>((static_cast<std::uint64_t>(remaining) * 5U) /
                                                  std::max<std::uint32_t>(1U, chart.max_tick));
            rectangle(surface, x - arm, y, x + arm, y,
                      Color{64U, 240U, 192U, 224U}, state.alpha);
            rectangle(surface, x, y - arm, x, y + arm,
                      Color{64U, 240U, 192U, 224U}, state.alpha);
        }
        if (state.touches[sensor].present) {
            const Color color = state.touches[sensor].hold
                                    ? Color{96U, 255U, 176U, 240U}
                                    : Color{96U, 224U, 255U, 240U};
            blend(surface, x, y, color, state.alpha);
            blend(surface, x - 1, y, color, state.alpha);
            blend(surface, x + 1, y, color, state.alpha);
            blend(surface, x, y - 1, color, state.alpha);
            blend(surface, x, y + 1, color, state.alpha);
        }
    }
    for (std::size_t lane = 0; lane < 8U; ++lane) {
        if (state.taps[lane].present) {
            const int x = lane_x(state, lane);
            rectangle(surface, x - 3, 71, x + 3, 73,
                      gray(208U + (state.taps[lane].strength >> 4U)), state.alpha);
        }
    }
    for (std::size_t lane = 0; lane < 8U; ++lane) {
        if (!state.breaks[lane].present) {
            continue;
        }
        const int x = lane_x(state, lane);
        const Color color = gray(240U + (state.breaks[lane].strength >> 6U));
        rectangle(surface, x - 4, 48, x + 4, 48, color, state.alpha);
        if (state.breaks[lane].flash_frames > 0U) {
            blend(surface, x, 44, color, state.alpha);
            blend(surface, x, 45, color, state.alpha);
            blend(surface, x, 46, color, state.alpha);
            blend(surface, x, 47, color, state.alpha);
            blend(surface, x, 49, color, state.alpha);
            blend(surface, x, 50, color, state.alpha);
            blend(surface, x, 51, color, state.alpha);
            blend(surface, x, 52, color, state.alpha);
        }
    }
    std::uint8_t break_flash_frames = 0;
    for (const LatestBreak& note_break : state.breaks) {
        break_flash_frames = std::max(break_flash_frames, note_break.flash_frames);
    }
    if (break_flash_frames > 0U) {
        const Color color = gray(224U + break_flash_frames);
        rectangle(surface, 0, 0, kCanvasWidth - 1, 0, color, state.alpha);
        rectangle(surface, 0, kCanvasHeight - 1, kCanvasWidth - 1, kCanvasHeight - 1,
                   color, state.alpha);
        rectangle(surface, 0, 1, 0, kCanvasHeight - 2, color, state.alpha);
        rectangle(surface, kCanvasWidth - 1, 1, kCanvasWidth - 1, kCanvasHeight - 2,
                  color, state.alpha);
    }
    if (state.warning_count > 0U) {
        const Color color = gray(192U + 16U * state.latest_warning_severity);
        for (std::uint32_t x = 0; x < std::min<std::uint32_t>(kCanvasWidth, state.warning_count); ++x) {
            blend(surface, static_cast<int>(x), 0, color, state.alpha);
        }
    }
    for (std::size_t j = 0; j < 8U; ++j) {
        const std::uint8_t byte = static_cast<std::uint8_t>((state.phase >> (8U * j)) & 0xffU);
        rectangle(surface, static_cast<int>(8U + j), 143 - (byte % 16U),
                  static_cast<int>(9U + j), 144 - (byte % 16U),
                  gray(32U + (byte % 224U)), state.alpha);
    }
}

}

Bytes render_rgba(std::uint64_t chart_id, const ParsedChart& chart, const State& state,
                  std::size_t frame) {
    Bytes pixels(kRgbaBytes);
    render_impl(chart_id, chart, state, frame, false, pixels);
    return pixels;
}

Bytes render_output_rgba(std::uint64_t chart_id, const ParsedChart& chart,
                          const State& state, std::size_t frame) {
    Bytes pixels(kOutputFramePayloadBytes);
    render_output_rgba_into(
        chart_id, chart, state, frame,
        std::span<std::uint8_t, kOutputFramePayloadBytes>(pixels.data(), pixels.size()));
    return pixels;
}

void render_output_rgba_into(
    std::uint64_t chart_id, const ParsedChart& chart, const State& state, std::size_t frame,
    std::span<std::uint8_t, kOutputFramePayloadBytes> output) {
    render_impl(chart_id, chart, state, frame, true, output);
}

Bytes render_gray8(std::uint64_t chart_id, const ParsedChart& chart, const State& state,
                   std::size_t frame) {
    const Bytes rgba = render_rgba(chart_id, chart, state, frame);
    Bytes grayscale(kGrayBytes);
    for (std::size_t pixel = 0; pixel < grayscale.size(); ++pixel) {
        const std::size_t offset = pixel * 4U;
        const std::uint32_t value = 77U * rgba[offset] + 150U * rgba[offset + 1U] +
                                    29U * rgba[offset + 2U] + 128U;
        grayscale[pixel] = static_cast<std::uint8_t>(value >> 8U);
    }
    return grayscale;
}

Bytes sample_output_frame(std::span<const std::uint8_t> rgba) {
    if (rgba.size() != kRgbaBytes) {
        throw std::invalid_argument("source RGBA frame has the wrong size");
    }
    Bytes sampled(kOutputFramePayloadBytes);
    for (std::size_t y = 0; y < kOutputFrameHeight; ++y) {
        for (std::size_t x = 0; x < kOutputFrameWidth; ++x) {
            const std::size_t source_x = 4U * x + 2U;
            const std::size_t source_y = 4U * y + 2U;
            const std::size_t source = (source_y * kCanvasWidth + source_x) * 4U;
            const std::size_t target = (y * kOutputFrameWidth + x) * 4U;
            std::copy_n(rgba.begin() + static_cast<std::ptrdiff_t>(source), 4U,
                        sampled.begin() + static_cast<std::ptrdiff_t>(target));
        }
    }
    return sampled;
}

}
