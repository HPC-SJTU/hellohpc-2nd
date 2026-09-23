#include "MaiMoeEngine.hpp"

#include "maimoe/render.hpp"
#include "maimoe/sha256.hpp"
#include "maimoe/simai.hpp"
#include "maimoe/state.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>

#if !defined(_WIN32)
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace maimoe::engine {
namespace {

constexpr std::array<std::uint8_t, 8> kFrameMagic = {'M', 'M', 'F', 'R', 'M', 0, 0, 3};
constexpr std::array<std::uint8_t, 8> kExpectedMagic = {'M', 'A', 'I', 'E', 'X', 'P', 0, 3};
constexpr std::array<std::uint8_t, 4> kResultMagic = {'M', 'M', 'R', '3'};
constexpr std::uint16_t kExpectedHeaderBytes = 64U;
constexpr std::uint16_t kExpectedVersion = 3U;

void append_digest(Bytes& bytes, const Digest& digest) {
    bytes.insert(bytes.end(), digest.begin(), digest.end());
}

template <typename UInt>
void append_le_into(std::span<std::uint8_t> output, std::size_t& offset, UInt value) {
    static_assert(std::is_unsigned_v<UInt>);
    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
        output[offset++] = static_cast<std::uint8_t>(value & static_cast<UInt>(0xffU));
        value >>= 8U;
    }
}

void append_digest_into(std::span<std::uint8_t> output, std::size_t& offset,
                        const Digest& digest) {
    std::memcpy(output.data() + offset, digest.data(), digest.size());
    offset += digest.size();
}

void fill_stem(std::array<char, 16>& stem, std::uint64_t chart_id) noexcept {
    static constexpr char kHexDigits[] = "0123456789abcdef";
    for (std::size_t index = 0; index < stem.size(); ++index) {
        stem[stem.size() - 1U - index] = kHexDigits[static_cast<std::size_t>(chart_id & 0xfU)];
        chart_id >>= 4U;
    }
}

[[nodiscard]] Digest read_digest(std::span<const std::uint8_t> bytes, std::size_t& offset) {
    if (offset > bytes.size() || bytes.size() - offset < Digest{}.size()) {
        throw FormatError("truncated digest");
    }
    Digest result{};
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), result.size(), result.begin());
    offset += result.size();
    return result;
}

void require_zero(std::span<const std::uint8_t> bytes, std::size_t offset,
                  std::size_t count, std::string_view field) {
    if (offset > bytes.size() || bytes.size() - offset < count ||
        std::any_of(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                    bytes.begin() + static_cast<std::ptrdiff_t>(offset + count),
                    [](std::uint8_t value) { return value != 0U; })) {
        throw FormatError(std::string(field) + " must be zero");
    }
}

[[nodiscard]] Bytes read_file(const std::filesystem::path& path,
                              std::uint64_t expected_bytes) {
    if (expected_bytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw FormatError("file is too large for this platform");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw FormatError("cannot open " + path.string());
    }
    Bytes bytes(static_cast<std::size_t>(expected_bytes));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size()) ||
        input.peek() != std::char_traits<char>::eof()) {
        throw FormatError("file size changed while reading " + path.string());
    }
    return bytes;
}

[[nodiscard]] Bytes canonical_error_fields(const SemanticError& error) {
    if (!valid_semantic_error_code(error.code) || error.location.line == 0U ||
        error.location.column == 0U ||
        error.location.column > std::numeric_limits<std::uint16_t>::max()) {
        throw FormatError("semantic error cannot be encoded");
    }
    Bytes fields;
    fields.reserve(16U);
    append_le(fields, static_cast<std::uint16_t>(error.code));
    append_le(fields, static_cast<std::uint16_t>(error.location.column));
    append_le(fields, error.location.line);
    append_le(fields, error.detail);
    append_le(fields, std::uint32_t{0});
    return fields;
}

[[nodiscard]] Digest valid_result_digest(std::uint64_t chart_id, const Counts& counts,
                                         std::span<const std::uint8_t> state9,
                                         const Digest& frame_begin, const Digest& frame_end) {
    Bytes input;
    input.reserve(256U + state9.size());
    append_lp32(input, "MaiMoeResult/v3");
    append_le(input, chart_id);
    append_le(input, counts.total);
    append_le(input, counts.tap);
    append_le(input, counts.slide);
    append_le(input, counts.hold);
    append_le(input, counts.touch);
    append_le(input, counts.break_count);
    append_le(input, counts.warning);
    append_lp32(input, state9);
    append_lp32(input, std::span<const std::uint8_t>(frame_begin));
    append_lp32(input, std::span<const std::uint8_t>(frame_end));
    return sha256(input);
}

[[nodiscard]] Bytes error_payload(std::uint64_t chart_id, FrameRole role,
                                  std::span<const std::uint8_t> fields) {
    Bytes seed_input;
    append_lp32(seed_input, "MaiMoeErrorFrame/v3");
    append_le(seed_input, chart_id);
    seed_input.push_back(static_cast<std::uint8_t>(role));
    append_lp32(seed_input, fields);
    const Digest seed = sha256(seed_input);
    Bytes payload;
    payload.reserve(kOutputFramePayloadBytes);
    while (payload.size() < kOutputFramePayloadBytes) {
        payload.insert(payload.end(), seed.begin(), seed.end());
    }
    return payload;
}

[[nodiscard]] Digest error_result_digest(std::uint64_t chart_id,
                                         std::span<const std::uint8_t> fields,
                                         const Digest& frame_begin, const Digest& frame_end) {
    Bytes input;
    append_lp32(input, "MaiMoeError/v3");
    append_le(input, chart_id);
    append_lp32(input, fields);
    append_lp32(input, std::span<const std::uint8_t>(frame_begin));
    append_lp32(input, std::span<const std::uint8_t>(frame_end));
    return sha256(input);
}

void encode_frame_into(std::span<std::uint8_t> output, std::uint64_t chart_id, FrameRole role,
                       ResultStatus status, std::span<const std::uint8_t> payload,
                       const Digest& digest) {
    if ((role != FrameRole::Begin && role != FrameRole::End) ||
        payload.size() != kOutputFramePayloadBytes || output.size() != kOutputFrameFileBytes) {
        throw FormatError("frame role or payload size is invalid");
    }
    std::size_t offset = 0;
    std::memcpy(output.data(), kFrameMagic.data(), kFrameMagic.size());
    offset += kFrameMagic.size();
    append_le_into(output, offset, static_cast<std::uint16_t>(kOutputFrameHeaderBytes));
    append_le_into(output, offset, kOutputFrameWidth);
    append_le_into(output, offset, kOutputFrameHeight);
    output[offset++] = 2U;
    output[offset++] = static_cast<std::uint8_t>(role);
    append_le_into(output, offset, chart_id);
    output[offset++] = static_cast<std::uint8_t>(status);
    output[offset++] = 0U;
    append_le_into(output, offset, std::uint16_t{0});
    append_le_into(output, offset, kOutputFramePayloadBytes);
    append_digest_into(output, offset, digest);
    std::memcpy(output.data() + offset, payload.data(), payload.size());
    offset += payload.size();
    if (offset != kOutputFrameFileBytes) {
        throw FormatError("internal frame encoder size mismatch");
    }
}

[[nodiscard]] Bytes encode_frame(std::uint64_t chart_id, FrameRole role,
                                 ResultStatus status, std::span<const std::uint8_t> payload,
                                 const Digest& digest) {
    Bytes output(kOutputFrameFileBytes);
    encode_frame_into(std::span<std::uint8_t>(output.data(), output.size()), chart_id, role,
                      status, payload, digest);
    return output;
}

void encode_result_into(std::span<std::uint8_t> output, std::uint64_t chart_id,
                        ResultStatus status, const Counts& counts,
                        std::span<const std::uint8_t> error_fields, const Digest& frame_begin,
                        const Digest& frame_end, const Digest& result) {
    if ((status == ResultStatus::Valid && !error_fields.empty()) ||
        (status == ResultStatus::Error && error_fields.size() != 16U) ||
        output.size() != kOutputResultFileBytes) {
        throw FormatError("result union fields are inconsistent");
    }
    std::size_t offset = 0;
    std::memcpy(output.data(), kResultMagic.data(), kResultMagic.size());
    offset += kResultMagic.size();
    output[offset++] = static_cast<std::uint8_t>(status);
    output[offset++] = 0U;
    append_le_into(output, offset, static_cast<std::uint16_t>(kOutputResultFileBytes));
    append_le_into(output, offset, chart_id);
    append_le_into(output, offset, counts.total);
    append_le_into(output, offset, counts.tap);
    append_le_into(output, offset, counts.slide);
    append_le_into(output, offset, counts.hold);
    append_le_into(output, offset, counts.touch);
    append_le_into(output, offset, counts.break_count);
    append_le_into(output, offset, counts.warning);
    if (error_fields.empty()) {
        std::fill_n(output.data() + offset, 16U, std::uint8_t{0});
        offset += 16U;
    } else {
        std::memcpy(output.data() + offset, error_fields.data(), error_fields.size());
        offset += error_fields.size();
    }
    append_digest_into(output, offset, frame_begin);
    append_digest_into(output, offset, frame_end);
    append_digest_into(output, offset, result);
    std::fill_n(output.data() + offset, 4U, std::uint8_t{0});
    offset += 4U;
    if (offset != kOutputResultFileBytes) {
        throw FormatError("internal result encoder size mismatch");
    }
}

[[nodiscard]] Bytes encode_result(std::uint64_t chart_id, ResultStatus status,
                                  const Counts& counts, std::span<const std::uint8_t> error_fields,
                                  const Digest& frame_begin, const Digest& frame_end,
                                  const Digest& result) {
    Bytes output(kOutputResultFileBytes);
    encode_result_into(std::span<std::uint8_t>(output.data(), output.size()), chart_id, status,
                       counts, error_fields, frame_begin, frame_end, result);
    return output;
}

struct DecodedResult {
    std::uint64_t chart_id = 0;
    ResultStatus status = ResultStatus::Valid;
    Counts counts{};
    std::array<std::uint8_t, 16> error_fields{};
    Digest frame_begin{};
    Digest frame_end{};
    Digest result{};
};

[[nodiscard]] DecodedResult decode_result(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != kOutputResultFileBytes ||
        !std::equal(kResultMagic.begin(), kResultMagic.end(), bytes.begin())) {
        throw FormatError("invalid result file magic or size");
    }
    std::size_t offset = kResultMagic.size();
    const std::uint8_t status_value = bytes[offset++];
    const std::uint8_t flags = bytes[offset++];
    const std::uint16_t record_bytes = read_le<std::uint16_t>(bytes, offset);
    if ((status_value != static_cast<std::uint8_t>(ResultStatus::Valid) &&
         status_value != static_cast<std::uint8_t>(ResultStatus::Error)) ||
        flags != 0U || record_bytes != kOutputResultFileBytes) {
        throw FormatError("invalid result status, flags, or record size");
    }
    DecodedResult decoded;
    decoded.status = static_cast<ResultStatus>(status_value);
    decoded.chart_id = read_le<std::uint64_t>(bytes, offset);
    decoded.counts.total = read_le<std::uint32_t>(bytes, offset);
    decoded.counts.tap = read_le<std::uint32_t>(bytes, offset);
    decoded.counts.slide = read_le<std::uint32_t>(bytes, offset);
    decoded.counts.hold = read_le<std::uint32_t>(bytes, offset);
    decoded.counts.touch = read_le<std::uint32_t>(bytes, offset);
    decoded.counts.break_count = read_le<std::uint32_t>(bytes, offset);
    decoded.counts.warning = read_le<std::uint32_t>(bytes, offset);
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), decoded.error_fields.size(),
                decoded.error_fields.begin());
    offset += decoded.error_fields.size();
    decoded.frame_begin = read_digest(bytes, offset);
    decoded.frame_end = read_digest(bytes, offset);
    decoded.result = read_digest(bytes, offset);
    require_zero(bytes, offset, 4U, "result tail");
    offset += 4U;
    if (offset != bytes.size()) {
        throw FormatError("result record does not reach exact EOF");
    }
    const std::uint64_t category_count = static_cast<std::uint64_t>(decoded.counts.tap) +
        decoded.counts.slide + decoded.counts.hold + decoded.counts.touch +
        decoded.counts.break_count;
    const bool zero_error = std::all_of(decoded.error_fields.begin(), decoded.error_fields.end(),
                                        [](std::uint8_t value) { return value == 0U; });
    if ((decoded.status == ResultStatus::Valid &&
         (category_count != decoded.counts.total || !zero_error)) ||
        (decoded.status == ResultStatus::Error &&
         (decoded.counts != Counts{} || zero_error))) {
        throw FormatError("result valid/error union is inconsistent");
    }
    return decoded;
}

[[nodiscard]] Digest validate_frame(std::span<const std::uint8_t> bytes,
                                    std::uint64_t chart_id, FrameRole expected_role,
                                    ResultStatus status) {
    if (bytes.size() != kOutputFrameFileBytes ||
        !std::equal(kFrameMagic.begin(), kFrameMagic.end(), bytes.begin())) {
        throw FormatError("invalid frame file magic or size");
    }
    std::size_t offset = kFrameMagic.size();
    const std::uint16_t header_bytes = read_le<std::uint16_t>(bytes, offset);
    const std::uint16_t width = read_le<std::uint16_t>(bytes, offset);
    const std::uint16_t height = read_le<std::uint16_t>(bytes, offset);
    const std::uint8_t pixel_format = bytes[offset++];
    const std::uint8_t actual_role = bytes[offset++];
    const std::uint64_t actual_id = read_le<std::uint64_t>(bytes, offset);
    const std::uint8_t actual_status = bytes[offset++];
    const std::uint8_t flags = bytes[offset++];
    const std::uint16_t reserved = read_le<std::uint16_t>(bytes, offset);
    const std::uint32_t payload_bytes = read_le<std::uint32_t>(bytes, offset);
    const Digest declared_digest = read_digest(bytes, offset);
    if (header_bytes != kOutputFrameHeaderBytes || width != kOutputFrameWidth ||
        height != kOutputFrameHeight || pixel_format != 2U ||
        actual_role != static_cast<std::uint8_t>(expected_role) ||
        actual_id != chart_id || actual_status != static_cast<std::uint8_t>(status) ||
        flags != 0U || reserved != 0U || payload_bytes != kOutputFramePayloadBytes ||
        offset != kOutputFrameHeaderBytes) {
        throw FormatError("invalid frame header fields");
    }
    const Digest actual_digest = sha256(bytes.subspan(offset));
    if (actual_digest != declared_digest) {
        throw FormatError("frame payload SHA-256 mismatch");
    }
    return actual_digest;
}

#if defined(_WIN32)
[[nodiscard]] bool allocated_normally(const std::filesystem::path& path,
                                      std::uint64_t logical_bytes) {
    static_cast<void>(logical_bytes);
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0U &&
           (attributes & FILE_ATTRIBUTE_SPARSE_FILE) == 0U;
}
#else

[[nodiscard]] bool allocated_normally(int descriptor, const struct stat& status,
                                      std::uint64_t logical_bytes, const std::string& name) {
    bool allocated = status.st_blocks >= 0 &&
                     static_cast<std::uint64_t>(status.st_blocks) * 512U >= logical_bytes;
#if defined(SEEK_DATA) && defined(SEEK_HOLE)
    errno = 0;
    const off_t first_data = ::lseek(descriptor, 0, SEEK_DATA);
    if (first_data >= 0) {
        const off_t first_hole = ::lseek(descriptor, 0, SEEK_HOLE);
        allocated = first_data == 0 && first_hole >= 0 &&
                    static_cast<std::uint64_t>(first_hole) >= logical_bytes;
    } else if (errno != EINVAL) {
        allocated = false;
    }
    if (::lseek(descriptor, 0, SEEK_SET) != 0) {
        throw FormatError("cannot seek output file " + name);
    }
#endif
    return allocated;
}

class FileDescriptor {
public:
    explicit FileDescriptor(int descriptor = -1) noexcept : descriptor_(descriptor) {}
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept
        : descriptor_(std::exchange(other.descriptor_, -1)) {}
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            reset();
            descriptor_ = std::exchange(other.descriptor_, -1);
        }
        return *this;
    }
    ~FileDescriptor() { reset(); }

    [[nodiscard]] int get() const noexcept { return descriptor_; }

private:
    void reset() noexcept {
        if (descriptor_ >= 0) {
            static_cast<void>(::close(descriptor_));
            descriptor_ = -1;
        }
    }

    int descriptor_;
};

[[nodiscard]] FileDescriptor open_output_root(const std::filesystem::path& path) {
    const int descriptor =
        ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    if (descriptor < 0) {
        throw FormatError("output root must be a non-symlink directory");
    }
    return FileDescriptor(descriptor);
}

[[nodiscard]] FileDescriptor open_directory_at(int parent, const std::string& name) {
    const int descriptor =
        ::openat(parent, name.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    if (descriptor < 0) {
        throw FormatError("output contains a missing or invalid directory");
    }
    return FileDescriptor(descriptor);
}

[[nodiscard]] bool directory_matches_entry(int descriptor, int parent,
                                           const std::filesystem::path& name) {
    struct stat opened_status {};
    struct stat entry_status {};
    return ::fstat(descriptor, &opened_status) == 0 &&
           ::fstatat(parent, name.c_str(), &entry_status, AT_SYMLINK_NOFOLLOW) == 0 &&
           S_ISDIR(opened_status.st_mode) && S_ISDIR(entry_status.st_mode) &&
           opened_status.st_dev == entry_status.st_dev && opened_status.st_ino == entry_status.st_ino;
}

[[nodiscard]] std::set<std::string> directory_entries(int descriptor) {
    const int duplicate =
        ::openat(descriptor, ".", O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    if (duplicate < 0) {
        throw FormatError("cannot inspect output directory");
    }
    DIR* directory = ::fdopendir(duplicate);
    if (directory == nullptr) {
        static_cast<void>(::close(duplicate));
        throw FormatError("cannot inspect output directory");
    }

    std::set<std::string> entries;
    errno = 0;
    while (const dirent* entry = ::readdir(directory)) {
        const std::string name(entry->d_name);
        if (name != "." && name != ".." && !entries.insert(name).second) {
            static_cast<void>(::closedir(directory));
            throw FormatError("output directory contains duplicate entries");
        }
        errno = 0;
    }
    const int read_error = errno;
    static_cast<void>(::closedir(directory));
    if (read_error != 0) {
        throw FormatError("cannot inspect output directory");
    }
    return entries;
}

[[nodiscard]] Bytes read_output_file(int directory, const std::string& name,
                                     std::uint64_t expected_bytes) {
    const int descriptor = ::openat(directory, name.c_str(),
                                    O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0) {
        throw FormatError("cannot open output file " + name);
    }
    FileDescriptor file(descriptor);

    struct stat status {};
    if (::fstat(file.get(), &status) != 0 || !S_ISREG(status.st_mode) || status.st_nlink != 1 ||
        status.st_size < 0 || static_cast<std::uint64_t>(status.st_size) != expected_bytes) {
        throw FormatError("output contains an invalid, linked, or incorrectly sized file");
    }
    if (!allocated_normally(file.get(), status, expected_bytes, name)) {
        throw FormatError("output files must not be sparse");
    }
    if (expected_bytes > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw FormatError("file is too large for this platform");
    }

    Bytes bytes(static_cast<std::size_t>(expected_bytes));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = ::read(file.get(), bytes.data() + offset, bytes.size() - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
        } else if (count == 0) {
            throw FormatError("output file size changed while reading " + name);
        } else if (errno != EINTR) {
            throw FormatError("cannot read output file " + name);
        }
    }
    std::uint8_t trailing = 0;
    ssize_t trailing_count;
    do {
        trailing_count = ::read(file.get(), &trailing, 1U);
    } while (trailing_count < 0 && errno == EINTR);
    struct stat final_status {};
    struct stat path_status {};
    if (trailing_count != 0 || ::fstat(file.get(), &final_status) != 0 ||
        ::fstatat(directory, name.c_str(), &path_status, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISREG(final_status.st_mode) || final_status.st_nlink != 1 ||
        final_status.st_size < 0 ||
        static_cast<std::uint64_t>(final_status.st_size) != expected_bytes ||
        !allocated_normally(file.get(), final_status, expected_bytes, name) ||
        final_status.st_dev != status.st_dev || final_status.st_ino != status.st_ino ||
        path_status.st_dev != status.st_dev || path_status.st_ino != status.st_ino) {
        throw FormatError("output file changed while reading " + name);
    }
    return bytes;
}
#endif

}

std::string output_bucket(std::uint64_t chart_id) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(2) << (chart_id & 0xffU);
    return output.str();
}

std::string output_stem(std::uint64_t chart_id) {
    std::array<char, 16> stem{};
    fill_stem(stem, chart_id);
    return std::string(stem.data(), stem.size());
}

Manifest load_manifest(const std::filesystem::path& dataset_dir, bool verify_dataset) {
    const std::string text = read_chart(dataset_dir / "manifest.mmf",
                                        std::filesystem::file_size(dataset_dir / "manifest.mmf"));
    const Manifest manifest = parse_manifest_v4(text);
    if (verify_dataset) {
        static_cast<void>(validate_dataset_v4(dataset_dir, manifest, "manifest.mmf"));
    }
    return manifest;
}

std::string read_chart(const std::filesystem::path& path, std::uint64_t expected_bytes) {
    const Bytes bytes = read_file(path, expected_bytes);
    return std::string(bytes.begin(), bytes.end());
}

namespace {

struct ComputedChart {
    ResultStatus status = ResultStatus::Valid;
    Counts counts{};
    Bytes frame_begin_payload;
    Bytes frame_end_payload;
    Bytes error_fields;
    Digest frame_begin_digest{};
    Digest frame_end_digest{};
    Digest result{};
};

[[nodiscard]] ComputedChart compute_chart(std::uint64_t chart_id, std::string_view chart_text) {
    if (chart_id == 0U ||
        chart_id > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::invalid_argument("chart id must be in 1..2^63-1");
    }
    const ParsedChart chart = parse_maidata(chart_text);
    ComputedChart computed;
    std::array<std::uint8_t, kSerializedStateBytes> serialized_state{};
    if (const SemanticError* error = select_semantic_error(chart); error != nullptr) {
        computed.status = ResultStatus::Error;
        computed.error_fields = canonical_error_fields(*error);
        computed.frame_begin_payload = error_payload(chart_id, FrameRole::Begin,
                                                     computed.error_fields);
        computed.frame_end_payload = error_payload(chart_id, FrameRole::End,
                                                   computed.error_fields);
    } else {
        computed.counts = chart.counts;
        const EndpointStates states = evolve_endpoint_states(chart_id, chart);
        computed.frame_begin_payload.resize(kOutputFramePayloadBytes);
        computed.frame_end_payload.resize(kOutputFramePayloadBytes);
        render_output_rgba_into(
            chart_id, chart, states.frame_begin, kFirstSampleFrame,
            std::span<std::uint8_t, kOutputFramePayloadBytes>(
                computed.frame_begin_payload.data(), computed.frame_begin_payload.size()));
        render_output_rgba_into(
            chart_id, chart, states.frame_end, kLastSampleFrame,
            std::span<std::uint8_t, kOutputFramePayloadBytes>(
                computed.frame_end_payload.data(), computed.frame_end_payload.size()));
        serialize_state_into(states.frame_end, serialized_state);
    }

    computed.frame_begin_digest = sha256(computed.frame_begin_payload);
    computed.frame_end_digest = sha256(computed.frame_end_payload);
    if (computed.status == ResultStatus::Error) {
        computed.result = error_result_digest(chart_id, computed.error_fields,
                                              computed.frame_begin_digest,
                                              computed.frame_end_digest);
    } else {
        computed.result = valid_result_digest(chart_id, computed.counts, serialized_state,
                                              computed.frame_begin_digest,
                                              computed.frame_end_digest);
    }
    return computed;
}

}

ChartFiles process_chart(std::uint64_t chart_id, std::string_view chart_text) {
    ComputedChart computed = compute_chart(chart_id, chart_text);
    const std::string prefix = "charts/" + output_bucket(chart_id) + "/" + output_stem(chart_id);
    ChartFiles files;
    files.chart_id = chart_id;
    files.files[0] = EncodedFile{prefix + ".frame_begin.rgba",
                                 encode_frame(chart_id, FrameRole::Begin, computed.status,
                                              computed.frame_begin_payload,
                                              computed.frame_begin_digest)};
    files.files[1] = EncodedFile{prefix + ".frame_end.rgba",
                                 encode_frame(chart_id, FrameRole::End, computed.status,
                                              computed.frame_end_payload,
                                              computed.frame_end_digest)};
    files.files[2] = EncodedFile{prefix + ".result.mmr",
                                 encode_result(chart_id, computed.status, computed.counts,
                                               computed.error_fields, computed.frame_begin_digest,
                                               computed.frame_end_digest, computed.result)};
    return files;
}

void process_chart_into(std::uint64_t chart_id, std::string_view chart_text,
                        std::span<std::uint8_t, kChartEncodedBytes> storage,
                        EncodedChartLayout& layout) {
    const ComputedChart computed = compute_chart(chart_id, chart_text);
    layout.chart_id = chart_id;
    fill_stem(layout.stem, chart_id);
    layout.frame_begin_offset = 0U;
    layout.frame_end_offset = kOutputFrameFileBytes;
    layout.result_offset = 2U * kOutputFrameFileBytes;

    encode_frame_into(storage.subspan(0U, kOutputFrameFileBytes), chart_id, FrameRole::Begin,
                      computed.status, computed.frame_begin_payload,
                      computed.frame_begin_digest);
    encode_frame_into(storage.subspan(kOutputFrameFileBytes, kOutputFrameFileBytes), chart_id,
                      FrameRole::End, computed.status, computed.frame_end_payload,
                      computed.frame_end_digest);
    encode_result_into(storage.subspan(2U * kOutputFrameFileBytes, kOutputResultFileBytes),
                       chart_id, computed.status, computed.counts, computed.error_fields,
                       computed.frame_begin_digest, computed.frame_end_digest, computed.result);
}

Bytes write_expected(const ExpectedFile& expected) {
    if (expected.records.empty() || expected.records.size() > 16384U) {
        throw FormatError("expected file must contain 1..16384 records");
    }
    Bytes output;
    output.reserve(kExpectedHeaderBytes + expected.records.size() * kOutputResultFileBytes);
    output.insert(output.end(), kExpectedMagic.begin(), kExpectedMagic.end());
    append_le(output, kExpectedVersion);
    append_le(output, kExpectedHeaderBytes);
    append_le(output, static_cast<std::uint16_t>(kOutputResultFileBytes));
    append_le(output, std::uint16_t{0});
    append_le(output, static_cast<std::uint32_t>(expected.records.size()));
    append_digest(output, expected.manifest_sha256);
    output.insert(output.end(), 12U, 0U);
    std::uint64_t previous_id = 0;
    for (const Bytes& record : expected.records) {
        const DecodedResult decoded = decode_result(record);
        if (decoded.chart_id <= previous_id) {
            throw FormatError("expected records are not strictly id-ordered");
        }
        previous_id = decoded.chart_id;
        output.insert(output.end(), record.begin(), record.end());
    }
    return output;
}

ExpectedFile read_expected(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kExpectedHeaderBytes ||
        !std::equal(kExpectedMagic.begin(), kExpectedMagic.end(), bytes.begin())) {
        throw FormatError("invalid expected-file magic or size");
    }
    std::size_t offset = kExpectedMagic.size();
    const std::uint16_t version = read_le<std::uint16_t>(bytes, offset);
    const std::uint16_t header_bytes = read_le<std::uint16_t>(bytes, offset);
    const std::uint16_t record_bytes = read_le<std::uint16_t>(bytes, offset);
    const std::uint16_t flags = read_le<std::uint16_t>(bytes, offset);
    const std::uint32_t count = read_le<std::uint32_t>(bytes, offset);
    if (version != kExpectedVersion || header_bytes != kExpectedHeaderBytes ||
        record_bytes != kOutputResultFileBytes || flags != 0U || count == 0U ||
        count > 16384U || bytes.size() != kExpectedHeaderBytes +
            static_cast<std::size_t>(count) * kOutputResultFileBytes) {
        throw FormatError("invalid expected-file fields or record count");
    }
    ExpectedFile expected;
    expected.manifest_sha256 = read_digest(bytes, offset);
    require_zero(bytes, offset, 12U, "expected header reserved bytes");
    offset += 12U;
    expected.records.reserve(count);
    std::uint64_t previous_id = 0;
    for (std::uint32_t index = 0; index < count; ++index) {
        Bytes record(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                     bytes.begin() + static_cast<std::ptrdiff_t>(offset + kOutputResultFileBytes));
        const DecodedResult decoded = decode_result(record);
        if (decoded.chart_id <= previous_id) {
            throw FormatError("expected records are not strictly id-ordered");
        }
        previous_id = decoded.chart_id;
        expected.records.push_back(std::move(record));
        offset += kOutputResultFileBytes;
    }
    return expected;
}

ExpectedFile build_expected(const Manifest& manifest,
                            const std::filesystem::path& dataset_dir) {
    ExpectedFile expected;
    expected.manifest_sha256 = manifest.end_sha256;
    expected.records.reserve(manifest.charts.size());
    for (const ManifestChart& chart : manifest.charts) {
        ChartFiles files = process_chart(chart.id, read_chart(dataset_dir / chart.path, chart.bytes));
        expected.records.push_back(std::move(files.files[2].bytes));
    }
    return expected;
}

OutputValidation validate_output(const Manifest& manifest, const ExpectedFile& expected,
                                  const std::filesystem::path& output_dir) {
    if (expected.manifest_sha256 != manifest.end_sha256 ||
        expected.records.size() != manifest.charts.size()) {
        throw FormatError("expected file does not match the manifest");
    }
    std::set<std::string> required_directories{"charts"};
    for (std::uint32_t value = 0; value < 256U; ++value) {
        std::ostringstream bucket;
        bucket << "charts/" << std::hex << std::setfill('0') << std::setw(2) << value;
        required_directories.insert(bucket.str());
    }
    std::set<std::string> required_files;
    for (const ManifestChart& chart : manifest.charts) {
        const std::string prefix = "charts/" + output_bucket(chart.id) + "/" + output_stem(chart.id);
        required_files.insert(prefix + ".frame_begin.rgba");
        required_files.insert(prefix + ".frame_end.rgba");
        required_files.insert(prefix + ".result.mmr");
    }

#if defined(_WIN32)
    std::error_code error;
    const std::filesystem::file_status root_status =
        std::filesystem::symlink_status(output_dir, error);
    if (error || !std::filesystem::is_directory(root_status) ||
        std::filesystem::is_symlink(root_status)) {
        throw FormatError("output root must be a non-symlink directory");
    }
    std::set<std::string> seen_directories;
    std::set<std::string> seen_files;
    for (std::filesystem::recursive_directory_iterator iterator(output_dir, error), end;
         !error && iterator != end; iterator.increment(error)) {
        const std::filesystem::file_status status = iterator->symlink_status(error);
        if (error || std::filesystem::is_symlink(status)) {
            throw FormatError("output contains a symlink or unreadable entry");
        }
        const std::string relative = iterator->path().lexically_relative(output_dir).generic_string();
        if (std::filesystem::is_directory(status)) {
            if (!required_directories.contains(relative) || !seen_directories.insert(relative).second) {
                throw FormatError("output contains an unexpected directory");
            }
            continue;
        }
        if (!std::filesystem::is_regular_file(status) || !required_files.contains(relative) ||
            !seen_files.insert(relative).second ||
            std::filesystem::hard_link_count(iterator->path(), error) != 1U || error) {
            throw FormatError("output contains an invalid, linked, or unexpected file");
        }
    }
    if (error || seen_directories != required_directories || seen_files != required_files) {
        throw FormatError("output directory or file set is incomplete");
    }
#else
    FileDescriptor output_root = open_output_root(output_dir);
    if (directory_entries(output_root.get()) != std::set<std::string>{"charts"}) {
        throw FormatError("output contains an unexpected root entry");
    }
    FileDescriptor charts_directory = open_directory_at(output_root.get(), "charts");
    std::set<std::string> required_buckets;
    std::array<std::set<std::string>, 256> required_bucket_files;
    for (std::uint32_t value = 0; value < 256U; ++value) {
        std::ostringstream bucket;
        bucket << std::hex << std::setfill('0') << std::setw(2) << value;
        required_buckets.insert(bucket.str());
    }
    for (const ManifestChart& chart : manifest.charts) {
        const std::size_t bucket = static_cast<std::size_t>(chart.id & 0xffU);
        const std::string stem = output_stem(chart.id);
        required_bucket_files[bucket].insert(stem + ".frame_begin.rgba");
        required_bucket_files[bucket].insert(stem + ".frame_end.rgba");
        required_bucket_files[bucket].insert(stem + ".result.mmr");
    }
    if (directory_entries(charts_directory.get()) != required_buckets) {
        throw FormatError("output bucket directory set is incomplete or unexpected");
    }
    std::array<FileDescriptor, 256> bucket_directories;
    for (std::uint32_t value = 0; value < 256U; ++value) {
        std::ostringstream bucket;
        bucket << std::hex << std::setfill('0') << std::setw(2) << value;
        bucket_directories[value] = open_directory_at(charts_directory.get(), bucket.str());
        if (directory_entries(bucket_directories[value].get()) != required_bucket_files[value]) {
            throw FormatError("output file set is incomplete or unexpected");
        }
    }
#endif

    OutputValidation validation;
#if defined(_WIN32)
    validation.output_files = seen_files.size();
#else
    validation.output_files = required_files.size();
#endif
    for (std::size_t index = 0; index < manifest.charts.size(); ++index) {
        const ManifestChart& chart = manifest.charts[index];
#if defined(_WIN32)
        const std::string prefix = "charts/" + output_bucket(chart.id) + "/" + output_stem(chart.id);
        const std::filesystem::path frame_begin_path = output_dir / (prefix + ".frame_begin.rgba");
        const std::filesystem::path frame_end_path = output_dir / (prefix + ".frame_end.rgba");
        const std::filesystem::path result_path = output_dir / (prefix + ".result.mmr");
        if (!allocated_normally(frame_begin_path, kOutputFrameFileBytes) ||
            !allocated_normally(frame_end_path, kOutputFrameFileBytes) ||
            !allocated_normally(result_path, kOutputResultFileBytes)) {
            throw FormatError("output files must not be sparse");
        }
        const Bytes result_bytes = read_file(result_path, kOutputResultFileBytes);
#else
        const std::size_t bucket = static_cast<std::size_t>(chart.id & 0xffU);
        const std::string stem = output_stem(chart.id);
        const Bytes result_bytes = read_output_file(
            bucket_directories[bucket].get(), stem + ".result.mmr", kOutputResultFileBytes);
#endif
        if (result_bytes != expected.records[index]) {
            throw FormatError("result file does not match expected data");
        }
        const DecodedResult result = decode_result(result_bytes);
        if (result.chart_id != chart.id) {
            throw FormatError("result chart id does not match its path");
        }
#if defined(_WIN32)
        const Digest frame_begin =
            validate_frame(read_file(frame_begin_path, kOutputFrameFileBytes),
                           chart.id, FrameRole::Begin, result.status);
        const Digest frame_end =
            validate_frame(read_file(frame_end_path, kOutputFrameFileBytes),
                           chart.id, FrameRole::End, result.status);
#else
        const Digest frame_begin = validate_frame(
            read_output_file(bucket_directories[bucket].get(), stem + ".frame_begin.rgba",
                             kOutputFrameFileBytes),
            chart.id, FrameRole::Begin, result.status);
        const Digest frame_end = validate_frame(
            read_output_file(bucket_directories[bucket].get(), stem + ".frame_end.rgba",
                             kOutputFrameFileBytes),
            chart.id, FrameRole::End, result.status);
#endif
        if (frame_begin != result.frame_begin || frame_end != result.frame_end) {
            throw FormatError("frame and result SHA-256 values disagree");
        }
        if (result.status == ResultStatus::Valid) {
            ++validation.valid_charts;
        } else {
            ++validation.error_charts;
        }
        validation.output_bytes += kOutputBytesPerChart;
    }
#if !defined(_WIN32)
    if (!directory_matches_entry(output_root.get(), AT_FDCWD, output_dir) ||
        !directory_matches_entry(charts_directory.get(), output_root.get(), "charts") ||
        directory_entries(output_root.get()) != std::set<std::string>{"charts"} ||
        directory_entries(charts_directory.get()) != required_buckets) {
        throw FormatError("output directory changed while checking");
    }
    for (std::size_t value = 0; value < bucket_directories.size(); ++value) {
        std::ostringstream bucket;
        bucket << std::hex << std::setfill('0') << std::setw(2) << value;
        if (!directory_matches_entry(bucket_directories[value].get(), charts_directory.get(),
                                     bucket.str()) ||
            directory_entries(bucket_directories[value].get()) != required_bucket_files[value]) {
            throw FormatError("output directory changed while checking");
        }
    }
#endif
    if (validation.output_files != manifest.output_file_count ||
        validation.output_bytes != manifest.output_logical_bytes) {
        throw FormatError("output totals do not match the manifest");
    }
    return validation;
}

}
