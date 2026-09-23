#include "Schedule.hpp"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

namespace maimoe::host {
namespace {

[[nodiscard]] std::uint32_t bucket_of(const ManifestChart& chart) noexcept {
    return static_cast<std::uint32_t>(chart.id & 0xffU);
}

}

std::vector<std::size_t> make_work_order(const Manifest& manifest, SchedulePolicy policy,
                                         std::uint32_t partitions) {
    std::vector<std::size_t> order(manifest.charts.size());
    std::iota(order.begin(), order.end(), 0U);
    if (policy == SchedulePolicy::Manifest) {
        return order;
    }
    if (policy == SchedulePolicy::SizeDescending) {
        std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
            const ManifestChart& lhs = manifest.charts[left];
            const ManifestChart& rhs = manifest.charts[right];
            return lhs.bytes != rhs.bytes ? lhs.bytes > rhs.bytes : lhs.id < rhs.id;
        });
        return order;
    }
    if (policy == SchedulePolicy::WorkLpt) {
        std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
            const ManifestChart& lhs = manifest.charts[left];
            const ManifestChart& rhs = manifest.charts[right];
            return lhs.work_units != rhs.work_units ? lhs.work_units > rhs.work_units
                                                    : lhs.id < rhs.id;
        });
        return order;
    }

    std::sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) {
        const ManifestChart& lhs = manifest.charts[left];
        const ManifestChart& rhs = manifest.charts[right];
        const std::uint32_t left_bucket = bucket_of(lhs);
        const std::uint32_t right_bucket = bucket_of(rhs);
        if (left_bucket != right_bucket) {
            return left_bucket < right_bucket;
        }
        return lhs.bytes != rhs.bytes ? lhs.bytes > rhs.bytes : lhs.id < rhs.id;
    });
    if (policy == SchedulePolicy::BucketLocality || partitions <= 1U) {
        return order;
    }

    std::vector<std::vector<std::size_t>> lanes(partitions);
    for (const std::size_t index : order) {
        lanes[bucket_of(manifest.charts[index]) % partitions].push_back(index);
    }
    std::vector<std::size_t> interleaved;
    interleaved.reserve(order.size());
    for (std::size_t depth = 0U; interleaved.size() < order.size(); ++depth) {
        for (const std::vector<std::size_t>& lane : lanes) {
            if (depth < lane.size()) {
                interleaved.push_back(lane[depth]);
            }
        }
    }
    return interleaved;
}

std::vector<std::size_t> make_work_order_hybrid(const Manifest& manifest,
                                                SchedulePolicy primary,
                                                SchedulePolicy secondary,
                                                std::uint32_t weight,
                                                std::uint32_t partitions) {
    const std::vector<std::size_t> primary_order =
        make_work_order(manifest, primary, partitions);
    const std::vector<std::size_t> secondary_order =
        make_work_order(manifest, secondary, partitions);
    const std::size_t count = manifest.charts.size();
    std::vector<std::size_t> prefix = primary_order;
    std::size_t prefix_size = (static_cast<std::size_t>(weight) * count + 50U) / 100U;
    if (prefix_size > count) {
        prefix_size = count;
    }
    prefix.resize(prefix_size);
    std::vector<bool> taken(count, false);
    for (const std::size_t index : prefix) {
        taken[index] = true;
    }
    std::vector<std::size_t> result;
    result.reserve(count);
    result.insert(result.end(), prefix.begin(), prefix.end());
    for (const std::size_t index : secondary_order) {
        if (!taken[index]) {
            result.push_back(index);
        }
    }
    return result;
}

}
