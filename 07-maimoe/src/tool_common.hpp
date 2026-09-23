#pragma once

#include "maimoe/manifest.hpp"
#include "maimoe/types.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace maimoe::tool {

inline constexpr std::string_view kManifestName = "manifest.mmf";

[[nodiscard]] inline Bytes read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open " + path.string());
    }
    const std::string text((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    if (!input.eof() && input.fail()) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return Bytes(text.begin(), text.end());
}

[[nodiscard]] inline std::string read_text(const std::filesystem::path& path) {
    const Bytes bytes = read_bytes(path);
    return std::string(bytes.begin(), bytes.end());
}

inline void write_bytes(const std::filesystem::path& path,
                        std::span<const std::uint8_t> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot create " + path.string());
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("cannot write " + path.string());
    }
}

inline void write_text(const std::filesystem::path& path, std::string_view text) {
    write_bytes(path, std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

[[nodiscard]] inline std::uint64_t parse_u64(std::string_view text,
                                             std::string_view name) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument(std::string(name) + " must be an unsigned decimal integer");
    }
    return value;
}

inline void prepare_empty_directory(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        if (error || !std::filesystem::is_directory(path, error) || error) {
            throw std::runtime_error("output path is not a directory: " + path.string());
        }
        if (std::filesystem::directory_iterator(path, error) !=
                std::filesystem::directory_iterator{} || error) {
            throw std::runtime_error("output directory must be empty: " + path.string());
        }
        return;
    }
    if (!std::filesystem::create_directories(path, error) || error) {
        throw std::runtime_error("cannot create output directory: " + path.string());
    }
}

[[nodiscard]] inline Manifest load_manifest(const std::filesystem::path& dataset_dir,
                                            bool validate_dataset = true) {
    const std::string text = read_text(dataset_dir / kManifestName);
    const Manifest manifest = parse_manifest_v4(text);
    if (validate_dataset) {
        static_cast<void>(validate_dataset_v4(dataset_dir, manifest, kManifestName));
    }
    return manifest;
}

}
