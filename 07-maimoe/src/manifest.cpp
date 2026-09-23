#include "maimoe/manifest.hpp"

#include "maimoe/sha256.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace maimoe {
namespace {

inline constexpr std::uint32_t kMaxManifestCharts = 16384U;
inline constexpr std::size_t kMaxManifestLineBytes = 1024U;

template <typename UInt>
[[nodiscard]] UInt parse_uint(std::string_view text, std::size_t line) {
    if (text.empty() || (text.size() > 1U && text.front() == '0')) {
        throw FormatError("non-canonical unsigned decimal", line, 1);
    }
    UInt value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw FormatError("unsigned decimal is malformed or out of range", line, 1);
    }
    return value;
}

[[nodiscard]] std::vector<std::string_view> strict_lines(std::string_view text) {
    if (text.empty() || text.back() != '\n') {
        throw FormatError("manifest must be nonempty and end with LF");
    }
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    std::size_t line_number = 1;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const unsigned char byte = static_cast<unsigned char>(text[index]);
        if (byte != '\n' && (byte < 0x20U || byte > 0x7eU)) {
            throw FormatError("manifest must use printable ASCII and LF", line_number,
                              index - start + 1U);
        }
        if (byte != '\n') {
            continue;
        }
        const std::string_view line = text.substr(start, index - start);
        if (line.empty()) {
            throw FormatError("manifest does not permit empty lines", line_number, 1);
        }
        if (line.size() > kMaxManifestLineBytes) {
            throw FormatError("manifest line exceeds 1024 bytes", line_number,
                              kMaxManifestLineBytes + 1U);
        }
        if (line.back() == ' ') {
            throw FormatError("manifest line has trailing whitespace", line_number, line.size());
        }
        lines.push_back(line);
        start = index + 1U;
        ++line_number;
    }
    return lines;
}

[[nodiscard]] std::string_view value_after(std::string_view line, std::string_view prefix,
                                           std::size_t line_number) {
    if (!line.starts_with(prefix)) {
        throw FormatError("unexpected manifest field or field order", line_number, 1);
    }
    return line.substr(prefix.size());
}

[[nodiscard]] bool reserved_windows_component(std::string_view component) noexcept {
    const std::size_t dot = component.find('.');
    const std::string_view stem = component.substr(0, dot);
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") {
        return true;
    }
    return stem.size() == 4U &&
           (stem.starts_with("com") || stem.starts_with("lpt")) &&
           stem[3] >= '1' && stem[3] <= '9';
}

void validate_manifest_fields(const Manifest& manifest) {
    if (manifest.chart_format != "MM-SIMAI-1" ||
        manifest.output_format != "MM-FILES-3") {
        throw FormatError("manifest formats must be MM-SIMAI-1 and MM-FILES-3");
    }
    if (manifest.chart_count == 0U || manifest.chart_count > kMaxManifestCharts ||
        manifest.charts.size() != manifest.chart_count) {
        throw FormatError("manifest chart_count must match 1..16384 records");
    }
    if (manifest.frame_width != kOutputFrameWidth ||
        manifest.frame_height != kOutputFrameHeight ||
        manifest.frame_file_bytes != kOutputFrameFileBytes ||
        manifest.result_file_bytes != kOutputResultFileBytes ||
        manifest.output_file_count != output_file_count(manifest.chart_count) ||
        manifest.output_logical_bytes != output_logical_bytes(manifest.chart_count)) {
        throw FormatError("manifest output geometry or totals do not match MM-FILES-3");
    }
    std::uint64_t previous_id = 0;
    std::uint64_t logical_bytes = 0;
    std::string previous_path;
    for (const ManifestChart& chart : manifest.charts) {
        if (chart.id <= previous_id ||
            chart.id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
            chart.bytes == 0U || chart.bytes > kMaxChartBytes ||
            chart.work_units == 0U ||
            chart.work_units > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
            !valid_relative_path(chart.path) ||
            (!previous_path.empty() && chart.path == previous_path)) {
            throw FormatError("manifest chart records are invalid or not strictly id-ordered");
        }
        previous_id = chart.id;
        previous_path = chart.path;
        if (logical_bytes > kMaxDatasetLogicalBytes ||
            chart.bytes > kMaxDatasetLogicalBytes - logical_bytes) {
            throw FormatError("manifest chart bytes exceed the 512 MiB aggregate quota");
        }
        logical_bytes += chart.bytes;
    }
    std::vector<std::string_view> paths;
    paths.reserve(manifest.charts.size());
    for (const ManifestChart& chart : manifest.charts) {
        paths.push_back(chart.path);
    }
    std::sort(paths.begin(), paths.end());
    if (std::adjacent_find(paths.begin(), paths.end()) != paths.end()) {
        throw FormatError("manifest chart paths must be unique");
    }
}

[[nodiscard]] ManifestChart parse_chart_line(std::string_view line, std::size_t line_number) {
    constexpr std::string_view id_prefix = "chart id=";
    const std::size_t bytes_pos = line.find(" bytes=", id_prefix.size());
    const std::size_t work_pos = line.find(
        " work_units=", bytes_pos == std::string_view::npos ? 0U : bytes_pos + 1U);
    const std::size_t sha_pos = line.find(
        " sha256=", work_pos == std::string_view::npos ? 0U : work_pos + 1U);
    const std::size_t path_pos = line.find(
        " path=", sha_pos == std::string_view::npos ? 0U : sha_pos + 1U);
    if (!line.starts_with(id_prefix) || bytes_pos == std::string_view::npos ||
        work_pos == std::string_view::npos || sha_pos == std::string_view::npos ||
        path_pos == std::string_view::npos) {
        throw FormatError("malformed chart record", line_number, 1);
    }
    ManifestChart chart;
    chart.id = parse_uint<std::uint64_t>(
        line.substr(id_prefix.size(), bytes_pos - id_prefix.size()), line_number);
    chart.bytes = parse_uint<std::uint64_t>(
        line.substr(bytes_pos + 7U, work_pos - bytes_pos - 7U), line_number);
    chart.work_units = parse_uint<std::uint64_t>(
        line.substr(work_pos + 12U, sha_pos - work_pos - 12U), line_number);
    chart.sha256 = parse_digest_hex(line.substr(sha_pos + 8U, path_pos - sha_pos - 8U));
    chart.path = std::string(line.substr(path_pos + 6U));
    return chart;
}

[[nodiscard]] std::string manifest_prefix(const Manifest& manifest) {
    validate_manifest_fields(manifest);
    std::ostringstream out;
    out << "MAIMOE_MANIFEST 4\n"
        << "chart_format=MM-SIMAI-1\n"
        << "output_format=MM-FILES-3\n"
        << "chart_count=" << manifest.chart_count << '\n'
        << "frame_width=" << manifest.frame_width << '\n'
        << "frame_height=" << manifest.frame_height << '\n'
        << "frame_file_bytes=" << manifest.frame_file_bytes << '\n'
        << "result_file_bytes=" << manifest.result_file_bytes << '\n'
        << "output_file_count=" << manifest.output_file_count << '\n'
        << "output_logical_bytes=" << manifest.output_logical_bytes << '\n';
    for (const ManifestChart& chart : manifest.charts) {
        out << "chart id=" << chart.id << " bytes=" << chart.bytes
            << " work_units=" << chart.work_units
            << " sha256=" << digest_hex(chart.sha256) << " path=" << chart.path << '\n';
    }
    return out.str();
}

[[nodiscard]] Digest hash_file(const std::filesystem::path& path, std::uint64_t expected_bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw FormatError("cannot open declared dataset file");
    }
    Sha256 hasher;
    std::array<char, 65536> buffer{};
    std::uint64_t total = 0;
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) {
            const auto unsigned_count = static_cast<std::size_t>(count);
            hasher.update(std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(buffer.data()), unsigned_count));
            total += static_cast<std::uint64_t>(unsigned_count);
        }
    }
    if (!input.eof() || total != expected_bytes) {
        throw FormatError("dataset file read length does not match manifest");
    }
    return hasher.finish();
}

[[nodiscard]] bool is_reparse_point(const std::filesystem::path& path) noexcept {
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U;
#else
    static_cast<void>(path);
    return false;
#endif
}

}

bool valid_relative_path(std::string_view path) noexcept {
    if (path.empty() || path.size() > 240U || path.front() == '/' || path.back() == '/' ||
        path.find("//") != std::string_view::npos ||
        path.find('\\') != std::string_view::npos) {
        return false;
    }
    std::size_t component_start = 0;
    for (std::size_t index = 0; index <= path.size(); ++index) {
        if (index != path.size() && path[index] != '/') {
            const char character = path[index];
            const bool valid = (character >= 'a' && character <= 'z') ||
                               (character >= '0' && character <= '9') || character == '.' ||
                               character == '_' || character == '-';
            if (!valid) {
                return false;
            }
            continue;
        }
        const std::string_view component = path.substr(component_start, index - component_start);
        if (component.empty() || component.size() > 64U || component == "." || component == ".." ||
            component.back() == '.' || component.back() == ' ' ||
            !((component.front() >= 'a' && component.front() <= 'z') ||
              (component.front() >= '0' && component.front() <= '9')) ||
            reserved_windows_component(component)) {
            return false;
        }
        component_start = index + 1U;
    }
    return true;
}

Manifest parse_manifest_v4(std::string_view text) {
    const std::vector<std::string_view> lines = strict_lines(text);
    if (lines.size() < 12U || lines.front() != "MAIMOE_MANIFEST 4") {
        throw FormatError("manifest v4 header is missing or invalid", 1, 1);
    }
    Manifest manifest;
    manifest.chart_format = std::string(value_after(lines[1], "chart_format=", 2));
    manifest.output_format = std::string(value_after(lines[2], "output_format=", 3));
    manifest.chart_count = parse_uint<std::uint32_t>(
        value_after(lines[3], "chart_count=", 4), 4);
    manifest.frame_width = parse_uint<std::uint16_t>(
        value_after(lines[4], "frame_width=", 5), 5);
    manifest.frame_height = parse_uint<std::uint16_t>(
        value_after(lines[5], "frame_height=", 6), 6);
    manifest.frame_file_bytes = parse_uint<std::uint32_t>(
        value_after(lines[6], "frame_file_bytes=", 7), 7);
    manifest.result_file_bytes = parse_uint<std::uint32_t>(
        value_after(lines[7], "result_file_bytes=", 8), 8);
    manifest.output_file_count = parse_uint<std::uint64_t>(
        value_after(lines[8], "output_file_count=", 9), 9);
    manifest.output_logical_bytes = parse_uint<std::uint64_t>(
        value_after(lines[9], "output_logical_bytes=", 10), 10);
    if (manifest.chart_count == 0U || manifest.chart_count > kMaxManifestCharts ||
        lines.size() != static_cast<std::size_t>(manifest.chart_count) + 11U) {
        throw FormatError("chart_count is outside 1..16384 or mismatches record count", 4, 1);
    }
    manifest.charts.reserve(manifest.chart_count);
    for (std::uint32_t index = 0; index < manifest.chart_count; ++index) {
        manifest.charts.push_back(parse_chart_line(lines[10U + index], 11U + index));
    }
    const std::size_t end_line_number = lines.size();
    manifest.end_sha256 = parse_digest_hex(
        value_after(lines.back(), "end sha256=", end_line_number));
    const std::size_t end_offset = text.size() - lines.back().size() - 1U;
    if (sha256(text.substr(0, end_offset)) != manifest.end_sha256) {
        throw FormatError("manifest end SHA-256 does not match preceding bytes",
                          end_line_number, 12);
    }
    validate_manifest_fields(manifest);
    return manifest;
}

EncodedManifest write_manifest_v4(const Manifest& manifest) {
    EncodedManifest encoded;
    const std::string prefix = manifest_prefix(manifest);
    encoded.end_sha256 = sha256(prefix);
    encoded.text = prefix + "end sha256=" + digest_hex(encoded.end_sha256) + "\n";
    return encoded;
}

namespace {

DatasetValidation validate_dataset(const std::filesystem::path& root,
                                   const Manifest& manifest,
                                   std::string_view manifest_path,
                                   bool verify_contents) {
    validate_manifest_fields(manifest);
    const EncodedManifest canonical_manifest = write_manifest_v4(manifest);
    if (manifest.end_sha256 != canonical_manifest.end_sha256) {
        throw FormatError("supplied manifest end digest does not match its canonical fields");
    }
    if (!valid_relative_path(manifest_path)) {
        throw FormatError("manifest dataset path is not safe and canonical");
    }
    std::error_code error;
    const std::filesystem::file_status root_status = std::filesystem::symlink_status(root, error);
    if (error || !std::filesystem::is_directory(root_status) ||
        std::filesystem::is_symlink(root_status) || is_reparse_point(root)) {
        throw FormatError("dataset root must be a regular non-symlink directory");
    }

    std::map<std::string, const ManifestChart*> declared;
    for (const ManifestChart& chart : manifest.charts) {
        declared.emplace(chart.path, &chart);
    }
    std::map<std::string, bool> seen;
    const std::string manifest_name(manifest_path);
    bool saw_manifest = false;
    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::none, error);
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        throw FormatError("cannot traverse dataset root");
    }
    while (iterator != end) {
        const std::filesystem::file_status status = iterator->symlink_status(error);
        if (error || std::filesystem::is_symlink(status) || is_reparse_point(iterator->path())) {
            throw FormatError("dataset contains a symlink or unreadable entry");
        }
        if (std::filesystem::is_directory(status)) {
            iterator.increment(error);
            if (error) {
                throw FormatError("dataset traversal failed");
            }
            continue;
        }
        if (!std::filesystem::is_regular_file(status)) {
            throw FormatError("dataset contains a non-regular entry");
        }
        const std::uintmax_t link_count = std::filesystem::hard_link_count(iterator->path(), error);
        if (error || link_count != 1U) {
            throw FormatError("dataset regular files must not be hard-linked");
        }
        const std::string relative = iterator->path().lexically_relative(root).generic_string();
        if (!valid_relative_path(relative)) {
            throw FormatError("dataset contains a path unsafe on supported platforms");
        }
        if (relative == manifest_name) {
            if (saw_manifest) {
                throw FormatError("dataset contains duplicate manifest paths");
            }
            if (verify_contents) {
                const std::uintmax_t bytes = iterator->file_size(error);
                if (error || bytes != canonical_manifest.text.size()) {
                    throw FormatError("dataset manifest file size is not canonical");
                }
                std::ifstream input(iterator->path(), std::ios::binary);
                std::string actual((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
                if (actual != canonical_manifest.text) {
                    throw FormatError(
                        "dataset manifest file differs from supplied canonical manifest");
                }
            }
            saw_manifest = true;
            iterator.increment(error);
            if (error) {
                throw FormatError("dataset traversal failed");
            }
            continue;
        }
        const auto declared_it = declared.find(relative);
        if (declared_it == declared.end() || !seen.emplace(relative, true).second) {
            throw FormatError("dataset contains an undeclared or duplicate regular file");
        }
        const ManifestChart& chart = *declared_it->second;
        const std::uintmax_t bytes = iterator->file_size(error);
        if (error || bytes != chart.bytes ||
            (verify_contents && hash_file(iterator->path(), chart.bytes) != chart.sha256)) {
            throw FormatError("dataset file size or SHA-256 mismatches manifest");
        }
        iterator.increment(error);
        if (error) {
            throw FormatError("dataset traversal failed");
        }
    }
    if (!saw_manifest || seen.size() != manifest.charts.size()) {
        throw FormatError("dataset is missing its manifest or a declared chart file");
    }
    std::uint64_t logical_bytes = 0;
    for (const ManifestChart& chart : manifest.charts) {
        logical_bytes += chart.bytes;
    }
    return DatasetValidation{manifest.chart_count, logical_bytes};
}

}

DatasetValidation validate_dataset_v4(const std::filesystem::path& root,
                                       const Manifest& manifest,
                                       std::string_view manifest_path) {
    return validate_dataset(root, manifest, manifest_path, true);
}

DatasetValidation validate_dataset_layout_v4(const std::filesystem::path& root,
                                              const Manifest& manifest,
                                              std::string_view manifest_path) {
    return validate_dataset(root, manifest, manifest_path, false);
}

}
