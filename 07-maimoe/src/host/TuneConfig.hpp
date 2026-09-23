#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace maimoe::host {

enum class SchedulePolicy {
    Manifest,
    SizeDescending,
    WorkLpt,
    BucketLocality,
    BucketInterleave,
};

enum class PartitionSteal {
    Strict,
    PreferLocal,
    Shared,
};

struct TuneConfig {
    std::uint32_t cpu_workers = 1U;
    std::uint32_t input_readers = 0U;
    std::uint32_t prefetch_depth = 0U;
    std::uint32_t compute_batch = 1U;
    std::uint32_t pipeline_mib = 8U;
    std::uint32_t io_workers = 0U;
    std::uint32_t write_batch = 1U;
    std::uint32_t queue_partitions = 1U;
    PartitionSteal partition_steal = PartitionSteal::Shared;
    SchedulePolicy schedule_policy = SchedulePolicy::Manifest;
    SchedulePolicy schedule_secondary = SchedulePolicy::Manifest;
    std::uint32_t schedule_hybrid_weight = 100U;
};

[[nodiscard]] TuneConfig load_tune(const std::filesystem::path& path);
[[nodiscard]] SchedulePolicy parse_schedule_policy(std::string_view value);
[[nodiscard]] const char* schedule_policy_name(SchedulePolicy policy) noexcept;

}
