#pragma once

#include "maimoe/manifest.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace maimoe::engine {

enum class ResultStatus : std::uint8_t { Valid = 1, Error = 2 };

struct EncodedFile {
    std::string relative_path;
    Bytes bytes;
};

struct ChartFiles {
    std::uint64_t chart_id = 0;
    std::array<EncodedFile, 3> files;
};

inline constexpr std::size_t kChartEncodedBytes = 18720U;

struct EncodedChartLayout {
    std::uint64_t chart_id = 0;
    std::array<char, 16> stem{};
    std::uint32_t frame_begin_offset = 0;
    std::uint32_t frame_end_offset = 0;
    std::uint32_t result_offset = 0;
};

struct ExpectedFile {
    Digest manifest_sha256{};
    std::vector<Bytes> records;
};

struct OutputValidation {
    std::uint32_t valid_charts = 0;
    std::uint32_t error_charts = 0;
    std::uint64_t output_files = 0;
    std::uint64_t output_bytes = 0;
};

[[nodiscard]] Manifest load_manifest(const std::filesystem::path& dataset_dir,
                                     bool verify_dataset = false);
[[nodiscard]] std::string read_chart(const std::filesystem::path& path,
                                     std::uint64_t expected_bytes);
[[nodiscard]] ChartFiles process_chart(std::uint64_t chart_id, std::string_view chart_text);
void process_chart_into(std::uint64_t chart_id, std::string_view chart_text,
                        std::span<std::uint8_t, kChartEncodedBytes> storage,
                        EncodedChartLayout& layout);
[[nodiscard]] std::string output_bucket(std::uint64_t chart_id);
[[nodiscard]] std::string output_stem(std::uint64_t chart_id);

[[nodiscard]] Bytes write_expected(const ExpectedFile& expected);
[[nodiscard]] ExpectedFile read_expected(std::span<const std::uint8_t> bytes);
[[nodiscard]] ExpectedFile build_expected(const Manifest& manifest,
                                          const std::filesystem::path& dataset_dir);

[[nodiscard]] OutputValidation validate_output(
    const Manifest& manifest, const ExpectedFile& expected,
    const std::filesystem::path& output_dir);

}
