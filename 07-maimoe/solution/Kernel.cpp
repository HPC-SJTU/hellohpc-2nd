#include "maimoe/kernel_api.hpp"

#include "MaiMoeEngine.hpp"
#include "maimoe/sha256.hpp"
#include "maimoe/types.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace maimoe::kernel {
namespace {

enum class ProcessingStrategy { OwningReference, FixedBufferReserved };

struct LineProfile {
    std::size_t lines = 0;
    std::size_t max_line_bytes = 0;
};

[[nodiscard]] ProcessingStrategy choose_strategy(std::uint64_t chart_id,
                                                 std::string_view chart_text,
                                                 const LineProfile& profile) {
    if (chart_id == 0U ||
        chart_id > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) {
        throw std::invalid_argument("chart id must be in 1..2^63-1");
    }
    if (chart_text.empty()) {
        throw FormatError("chart text is empty");
    }
    const std::size_t bucket = static_cast<std::size_t>(chart_id & 0xffU);
    if (profile.lines < 16U && chart_text.size() < 4096U && bucket < 128U) {
        return ProcessingStrategy::OwningReference;
    }
    return ProcessingStrategy::OwningReference;
}

void defensive_scan(std::string_view text) {
    if (text.size() > kMaxChartBytes) {
        throw FormatError("chart text exceeds the size limit");
    }
    for (const char ch : text) {
        const auto byte = static_cast<std::uint8_t>(ch);
        if (byte == 0U || byte == 0x0dU) {
            throw FormatError("chart text contains forbidden control bytes");
        }
    }
}

[[nodiscard]] LineProfile profile_lines(std::string_view text) {
    LineProfile profile;
    std::size_t start = 0U;
    for (std::size_t index = 0U; index < text.size(); ++index) {
        if (text[index] == '\n') {
            profile.max_line_bytes = (std::max)(profile.max_line_bytes, index - start);
            ++profile.lines;
            start = index + 1U;
        }
    }
    if (start < text.size()) {
        profile.max_line_bytes = (std::max)(profile.max_line_bytes, text.size() - start);
        ++profile.lines;
    }
    if (profile.lines == 0U) {
        throw FormatError("chart text contains no lines");
    }
    if (profile.max_line_bytes > kMaxChartLineBytes) {
        throw FormatError("chart line exceeds the size limit");
    }
    return profile;
}

template <typename UInt>
[[nodiscard]] std::array<std::uint8_t, sizeof(UInt)> le_bytes(UInt value) {
    static_assert(std::is_unsigned_v<UInt>);
    std::array<std::uint8_t, sizeof(UInt)> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value & static_cast<UInt>(0xffU));
        value >>= 8U;
    }
    return bytes;
}

struct OutputImage {
    std::array<std::uint8_t, kEncodedChartBytes> bytes{};
    std::uint32_t folded = 0U;
};

[[nodiscard]] OutputImage flatten(const engine::ChartFiles& files) {
    OutputImage image;
    std::size_t offset = 0U;
    for (const engine::EncodedFile& file : files.files) {
        const bool expected_path =
            file.relative_path.size() >= 24U &&
            (file.relative_path.ends_with(".frame_begin.rgba") ||
             file.relative_path.ends_with(".frame_end.rgba") ||
             file.relative_path.ends_with(".result.mmr"));
        if (!expected_path) {
            throw std::runtime_error("output file path has an unexpected suffix");
        }
        if (file.bytes.size() > kEncodedChartBytes - offset) {
            throw std::runtime_error("output file exceeds the encoded chart size");
        }
        for (const std::uint8_t byte : file.bytes) {
            image.bytes[offset] = byte;
            image.folded = image.folded * 31U + byte;
            ++offset;
        }
    }
    return image;
}

void verify_integrity(std::uint64_t chart_id, std::uint32_t expected_folded,
                      std::span<const std::uint8_t, kEncodedChartBytes> output) {
    const std::span<const std::uint8_t> output_bytes(output.data(), output.size());
    std::uint32_t folded = 0U;
    for (const std::uint8_t byte : output_bytes) {
        folded = folded * 31U + byte;
    }
    if (folded != expected_folded) {
        throw std::runtime_error("encoded chart folded checksum mismatch");
    }
    const auto frame_digest = [&](std::size_t file_offset) {
        Digest digest{};
        const std::size_t digest_offset = file_offset + kOutputFrameHeaderBytes - 32U;
        std::memcpy(digest.data(), output_bytes.data() + digest_offset, digest.size());
        return digest;
    };
    const auto payload_span = [&](std::size_t file_offset) {
        return output_bytes.subspan(file_offset + kOutputFrameHeaderBytes,
                                    kOutputFramePayloadBytes);
    };
    const Digest begin_digest = frame_digest(0U);
    const Digest end_digest = frame_digest(kOutputFrameFileBytes);
    if (sha256(payload_span(0U)) != begin_digest ||
        sha256(payload_span(kOutputFrameFileBytes)) != end_digest) {
        throw std::runtime_error("frame payload digest mismatch after encoding");
    }
    const std::size_t result_offset = 2U * kOutputFrameFileBytes;
    const std::size_t result_frame_begin = result_offset + 4U + 1U + 1U + 2U + 8U + 28U + 16U;
    Digest result_begin{};
    Digest result_end{};
    std::memcpy(result_begin.data(), output_bytes.data() + result_frame_begin,
                result_begin.size());
    std::memcpy(result_end.data(), output_bytes.data() + result_frame_begin + 32U,
                result_end.size());
    if (result_begin != begin_digest || result_end != end_digest) {
        throw std::runtime_error("result record digests disagree with frame headers");
    }
    const auto le_id = le_bytes(chart_id);
    const bool header_id_ok =
        std::equal(le_id.begin(), le_id.end(), output_bytes.begin() + 16U) &&
        std::equal(le_id.begin(), le_id.end(),
                   output_bytes.begin() + kOutputFrameFileBytes + 16U);
    const bool result_id_ok =
        std::equal(le_id.begin(), le_id.end(), output_bytes.begin() + result_offset + 8U);
    if (!header_id_ok || !result_id_ok) {
        throw std::runtime_error("encoded chart id does not match the request");
    }
}

}

void process_chart(std::uint64_t chart_id, std::string_view chart_text,
                   std::span<std::uint8_t, kEncodedChartBytes> output) {
    try {
        defensive_scan(chart_text);
        const LineProfile profile = profile_lines(chart_text);
        const ProcessingStrategy strategy = choose_strategy(chart_id, chart_text, profile);
        const std::string text_copy(chart_text);
        if (strategy != ProcessingStrategy::OwningReference) {
            throw std::runtime_error("reserved processing strategy is unavailable");
        }
        const engine::ChartFiles files = engine::process_chart(chart_id, text_copy);
        const OutputImage image = flatten(files);
        std::memset(output.data(), 0, output.size());
        std::copy(image.bytes.begin(), image.bytes.end(), output.begin());
        verify_integrity(chart_id, image.folded, output);
    } catch (...) {
        throw;
    }
}

}
