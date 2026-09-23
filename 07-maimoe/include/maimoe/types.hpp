#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace maimoe {

inline constexpr std::uint16_t kCanvasWidth = 256;
inline constexpr std::uint16_t kCanvasHeight = 144;
inline constexpr std::uint32_t kRgbaBytes =
    static_cast<std::uint32_t>(kCanvasWidth) * kCanvasHeight * 4U;
inline constexpr std::uint32_t kGrayBytes =
    static_cast<std::uint32_t>(kCanvasWidth) * kCanvasHeight;
inline constexpr std::uint32_t kTicksPerQuarter = 384;
inline constexpr std::uint32_t kTicksPerWhole = 4U * kTicksPerQuarter;
inline constexpr std::size_t kFrameCount = 10;
inline constexpr std::size_t kFirstSampleFrame = 0;
inline constexpr std::size_t kLastSampleFrame = kFrameCount - 1U;
inline constexpr std::size_t kTouchSensorCount = 33;
inline constexpr std::size_t kMaxChartBytes = 8U * 1024U * 1024U;
inline constexpr std::size_t kMaxChartLineBytes = 4096U;
inline constexpr std::size_t kMaxMetadataLineBytes = 255U;
inline constexpr std::uint32_t kMaxEvents = 200000U;
inline constexpr std::uint32_t kMaxTimelineTick = 1048575U;
inline constexpr std::uint32_t kMinBpmMilli = 30000U;
inline constexpr std::uint32_t kMaxBpmMilli = 400000U;
inline constexpr std::size_t kDenseEachThreshold = 4U;
inline constexpr std::uint64_t kMaxDatasetLogicalBytes = 512ULL * 1024ULL * 1024ULL;
inline constexpr std::uint16_t kOutputFrameWidth = 64U;
inline constexpr std::uint16_t kOutputFrameHeight = 36U;
inline constexpr std::uint32_t kOutputFramePayloadBytes =
    static_cast<std::uint32_t>(kOutputFrameWidth) * kOutputFrameHeight * 4U;
inline constexpr std::uint32_t kOutputFrameHeaderBytes = 64U;
inline constexpr std::uint32_t kOutputFrameFileBytes =
    kOutputFrameHeaderBytes + kOutputFramePayloadBytes;
inline constexpr std::uint32_t kOutputResultFileBytes = 160U;
inline constexpr std::uint32_t kOutputFilesPerChart = 3U;
inline constexpr std::uint64_t kOutputBytesPerChart =
    2ULL * kOutputFrameFileBytes + kOutputResultFileBytes;

enum class FrameRole : std::uint8_t {
    Begin = 1,
    End = 2,
};

[[nodiscard]] constexpr std::uint64_t output_file_count(std::uint32_t chart_count) noexcept {
    return static_cast<std::uint64_t>(chart_count) * kOutputFilesPerChart;
}

[[nodiscard]] constexpr std::uint64_t output_logical_bytes(
    std::uint32_t chart_count) noexcept {
    return static_cast<std::uint64_t>(chart_count) * kOutputBytesPerChart;
}

using Digest = std::array<std::uint8_t, 32>;
using Bytes = std::vector<std::uint8_t>;

class FormatError final : public std::runtime_error {
public:
    FormatError(std::string message, std::size_t line = 0, std::size_t column = 0);

    [[nodiscard]] std::size_t line() const noexcept { return line_; }
    [[nodiscard]] std::size_t column() const noexcept { return column_; }

private:
    std::size_t line_;
    std::size_t column_;
};

enum class EventType : std::uint8_t {
    Hold = 0,
    Slide = 1,
    TouchHold = 2,
    Tap = 3,
    TouchTap = 4,
    Break = 5,
};

struct SourceLocation {
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::uint32_t byte_offset = 0;

    auto operator<=>(const SourceLocation&) const = default;
};

struct Event {
    EventType type = EventType::Tap;
    std::uint32_t tick = 0;
    std::uint32_t end_tick = 0;
    std::uint8_t lane = 0;
    std::uint8_t target_lane = 0;
    std::uint8_t touch_sensor = 0;
    std::uint8_t strength = 0;
    std::uint8_t path_id = 0;
    SourceLocation location{};
    std::string canonical;
};

struct Counts {
    std::uint32_t total = 0;
    std::uint32_t tap = 0;
    std::uint32_t slide = 0;
    std::uint32_t hold = 0;
    std::uint32_t touch = 0;
    std::uint32_t break_count = 0;
    std::uint32_t warning = 0;

    auto operator<=>(const Counts&) const = default;
};

enum class WarningCode : std::uint16_t {
    EmptyChart = 1,
    RedundantBpm = 2,
    RedundantDivision = 3,
    DenseEach = 4,
};

struct Warning {
    WarningCode code = WarningCode::EmptyChart;
    std::uint8_t severity = 1;
    SourceLocation location{};
};

enum class SemanticErrorCode : std::uint16_t {
    BpmRange = 1,
    TimelineRange = 2,
    SlideSelf = 3,
    DuplicateSource = 4,
    ActiveOverlap = 5,
    EventLimit = 6,
};

struct SemanticError {
    SemanticErrorCode code = SemanticErrorCode::BpmRange;
    SourceLocation location{};
    std::uint32_t detail = 0;

    auto operator<=>(const SemanticError&) const = default;
};

struct Metadata {
    std::string title;
    std::string artist;
    std::string description;
    std::string level;
    std::uint32_t whole_bpm_milli = 120000;
};

struct ChartCell {
    std::string canonical;
    SourceLocation location{};
};

struct ParsedChart {
    Metadata metadata;
    std::vector<Event> events;
    Counts counts;
    std::vector<Warning> warnings;
    std::vector<SemanticError> semantic_candidates;
    std::vector<ChartCell> cells;
    std::uint32_t max_tick = 0;
};

[[nodiscard]] bool valid_semantic_error_code(SemanticErrorCode code) noexcept;

template <typename UInt>
void append_le(Bytes& out, UInt value) {
    static_assert(std::is_unsigned_v<UInt>);
    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
        out.push_back(static_cast<std::uint8_t>(value & static_cast<UInt>(0xffU)));
        value >>= 8U;
    }
}

template <typename UInt>
UInt read_le(std::span<const std::uint8_t> bytes, std::size_t& offset) {
    static_assert(std::is_unsigned_v<UInt>);
    if (offset > bytes.size() || bytes.size() - offset < sizeof(UInt)) {
        throw FormatError("truncated little-endian integer");
    }
    UInt value = 0;
    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
        value = static_cast<UInt>(
            value | static_cast<UInt>(static_cast<std::uint64_t>(bytes[offset + i]) << (8U * i)));
    }
    offset += sizeof(UInt);
    return value;
}

void append_lp32(Bytes& out, std::span<const std::uint8_t> value);
void append_lp32(Bytes& out, std::string_view value);

}
