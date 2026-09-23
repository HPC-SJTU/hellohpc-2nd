#include "TuneConfig.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace maimoe::host {
namespace {

[[nodiscard]] std::invalid_argument tune_error(std::string_view message) {
    return std::invalid_argument(std::string("KstroParam.toml: ") + std::string(message));
}

[[nodiscard]] std::uint32_t parse_uint(std::string_view text, std::string_view name,
                                       std::uint32_t minimum, std::uint32_t maximum) {
    if (text.empty() || (text.size() > 1U && text.front() == '0')) {
        throw tune_error(std::string(name) + " is not a canonical integer");
    }
    std::uint32_t value = 0U;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() ||
        value < minimum || value > maximum) {
        throw tune_error(std::string(name) + " is outside its allowed range");
    }
    return value;
}

[[nodiscard]] std::string_view trim_space(std::string_view text) {
    while (!text.empty() && text.front() == ' ') {
        text.remove_prefix(1U);
    }
    while (!text.empty() && text.back() == ' ') {
        text.remove_suffix(1U);
    }
    return text;
}

[[nodiscard]] bool valid_key(std::string_view key) {
    return !key.empty() && std::all_of(key.begin(), key.end(), [](char character) {
        return (character >= 'a' && character <= 'z') || character == '_';
    });
}

[[nodiscard]] std::string parse_enum(std::string_view text, std::string_view name) {
    if (text.size() < 2U || text.front() != '"' || text.back() != '"') {
        throw tune_error(std::string(name) + " must be a double-quoted string");
    }
    const std::string_view inner = text.substr(1U, text.size() - 2U);
    if (inner.empty() || std::any_of(inner.begin(), inner.end(), [](char character) {
            const auto byte = static_cast<unsigned char>(character);
            return character == '"' || character == '\\' || byte < 0x20U || byte > 0x7eU;
        })) {
        throw tune_error(std::string(name) + " contains an invalid string value");
    }
    return std::string(inner);
}

[[nodiscard]] std::pair<SchedulePolicy, SchedulePolicy> parse_schedule_value(
    std::string_view value) {
    const std::size_t colon = value.find(':');
    if (colon == std::string_view::npos) {
        const SchedulePolicy policy = parse_schedule_policy(value);
        return {policy, policy};
    }
    if (value.find(':', colon + 1U) != std::string_view::npos || colon == 0U ||
        colon + 1U == value.size()) {
        throw std::invalid_argument(
            "schedule_policy must be a policy or a primary:secondary pair");
    }
    return {parse_schedule_policy(value.substr(0U, colon)),
            parse_schedule_policy(value.substr(colon + 1U))};
}

[[nodiscard]] PartitionSteal parse_partition_steal(std::string_view value) {
    if (value == "strict") {
        return PartitionSteal::Strict;
    }
    if (value == "prefer_local") {
        return PartitionSteal::PreferLocal;
    }
    if (value == "shared") {
        return PartitionSteal::Shared;
    }
    throw std::invalid_argument(
        "partition_steal must be strict, prefer_local, or shared");
}

}

SchedulePolicy parse_schedule_policy(std::string_view value) {
    if (value == "manifest") {
        return SchedulePolicy::Manifest;
    }
    if (value == "size_desc") {
        return SchedulePolicy::SizeDescending;
    }
    if (value == "work_lpt") {
        return SchedulePolicy::WorkLpt;
    }
    if (value == "bucket_locality") {
        return SchedulePolicy::BucketLocality;
    }
    if (value == "bucket_interleave") {
        return SchedulePolicy::BucketInterleave;
    }
    throw std::invalid_argument(
        "schedule_policy must be manifest, size_desc, work_lpt, bucket_locality, "
        "or bucket_interleave");
}

const char* schedule_policy_name(SchedulePolicy policy) noexcept {
    switch (policy) {
        case SchedulePolicy::Manifest:
            return "manifest";
        case SchedulePolicy::SizeDescending:
            return "size_desc";
        case SchedulePolicy::WorkLpt:
            return "work_lpt";
        case SchedulePolicy::BucketLocality:
            return "bucket_locality";
        case SchedulePolicy::BucketInterleave:
            return "bucket_interleave";
    }
    return "unknown";
}

TuneConfig load_tune(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open KstroParam.toml");
    }
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (text.empty() || text.find('\r') != std::string::npos ||
        text.find('\0') != std::string::npos ||
        std::any_of(text.begin(), text.end(), [](unsigned char byte) {
            return byte != '\n' && (byte < 0x20U || byte > 0x7eU);
        })) {
        throw tune_error("KstroParam.toml must be LF-separated ASCII text");
    }

    text.push_back('\n');
    std::map<std::string, std::string> fields;
    std::size_t start = 0U;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        std::string_view line(text.data() + start, end - start);
        const std::size_t hash = line.find('#');
        if (hash != std::string_view::npos) {
            line = line.substr(0U, hash);
        }
        line = trim_space(line);
        if (!line.empty()) {
            const std::size_t equals = line.find('=');
            if (equals == std::string_view::npos ||
                line.find('=', equals + 1U) != std::string_view::npos) {
                throw tune_error("KstroParam.toml contains a malformed line");
            }
            const std::string key(trim_space(line.substr(0U, equals)));
            const std::string value(trim_space(line.substr(equals + 1U)));
            if (!valid_key(key) || value.empty() || !fields.emplace(key, value).second) {
                throw tune_error(value.empty() || !valid_key(key)
                                     ? "KstroParam.toml contains a malformed line"
                                     : "KstroParam.toml contains a duplicate key");
            }
        }
        start = end + 1U;
    }

    static constexpr std::array<std::string_view, 11> kRequired = {
        "cpu_workers", "input_readers", "prefetch_depth", "compute_batch",
        "pipeline_mib", "io_workers", "write_batch", "queue_partitions",
        "partition_steal", "schedule_policy", "schedule_hybrid_weight"};
    if (fields.size() != kRequired.size() ||
        std::any_of(kRequired.begin(), kRequired.end(), [&](std::string_view key) {
            return !fields.contains(std::string(key));
        })) {
        throw tune_error("KstroParam.toml contains missing or unknown keys");
    }

    TuneConfig config;
    config.cpu_workers = parse_uint(fields.at("cpu_workers"), "cpu_workers", 1U, 8U);
    config.input_readers =
        parse_uint(fields.at("input_readers"), "input_readers", 0U, 8U);
    config.prefetch_depth =
        parse_uint(fields.at("prefetch_depth"), "prefetch_depth", 0U, 256U);
    config.compute_batch =
        parse_uint(fields.at("compute_batch"), "compute_batch", 1U, 64U);
    config.pipeline_mib =
        parse_uint(fields.at("pipeline_mib"), "pipeline_mib", 1U, 256U);
    config.io_workers = parse_uint(fields.at("io_workers"), "io_workers", 0U, 32U);
    config.write_batch = parse_uint(fields.at("write_batch"), "write_batch", 1U, 64U);
    config.queue_partitions =
        parse_uint(fields.at("queue_partitions"), "queue_partitions", 1U, 32U);
    config.schedule_hybrid_weight = parse_uint(fields.at("schedule_hybrid_weight"),
                                               "schedule_hybrid_weight", 0U, 100U);
    try {
        config.partition_steal = parse_partition_steal(
            parse_enum(fields.at("partition_steal"), "partition_steal"));
        const auto [primary, secondary] =
            parse_schedule_value(parse_enum(fields.at("schedule_policy"), "schedule_policy"));
        config.schedule_policy = primary;
        config.schedule_secondary = secondary;
    } catch (const std::invalid_argument& error) {
        throw tune_error(error.what());
    }
    if ((config.input_readers == 0U) != (config.prefetch_depth == 0U)) {
        throw tune_error(
            "prefetch_depth must be 0 iff input_readers is 0, and at least 1 otherwise");
    }
    if (config.schedule_policy == config.schedule_secondary &&
        config.schedule_hybrid_weight != 100U) {
        throw tune_error(
            "schedule_hybrid_weight must be 100 for a single or identical schedule_policy");
    }
    return config;
}

}
