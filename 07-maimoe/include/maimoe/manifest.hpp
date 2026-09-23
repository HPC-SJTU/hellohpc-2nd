#pragma once

#include "maimoe/types.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace maimoe {

struct ManifestChart {
    std::uint64_t id = 0;
    std::uint64_t bytes = 0;
    std::uint64_t work_units = 0;
    Digest sha256{};
    std::string path;
};

struct Manifest {
    std::string chart_format = "MM-SIMAI-1";
    std::string output_format = "MM-FILES-3";
    std::uint32_t chart_count = 0;
    std::uint16_t frame_width = kOutputFrameWidth;
    std::uint16_t frame_height = kOutputFrameHeight;
    std::uint32_t frame_file_bytes = kOutputFrameFileBytes;
    std::uint32_t result_file_bytes = kOutputResultFileBytes;
    std::uint64_t output_file_count = 0;
    std::uint64_t output_logical_bytes = 0;
    std::vector<ManifestChart> charts;
    Digest end_sha256{};
};

struct EncodedManifest {
    std::string text;
    Digest end_sha256{};
};

struct DatasetValidation {
    std::uint32_t chart_count = 0;
    std::uint64_t logical_bytes = 0;
};

[[nodiscard]] Manifest parse_manifest_v4(std::string_view text);
[[nodiscard]] EncodedManifest write_manifest_v4(const Manifest& manifest);
[[nodiscard]] bool valid_relative_path(std::string_view path) noexcept;
[[nodiscard]] DatasetValidation validate_dataset_v4(
    const std::filesystem::path& root, const Manifest& manifest,
    std::string_view manifest_path = "manifest.mmf");
[[nodiscard]] DatasetValidation validate_dataset_layout_v4(
    const std::filesystem::path& root, const Manifest& manifest,
    std::string_view manifest_path = "manifest.mmf");

}
