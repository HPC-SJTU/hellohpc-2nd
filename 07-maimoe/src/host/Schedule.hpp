#pragma once

#include "TuneConfig.hpp"

#include "maimoe/manifest.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace maimoe::host {

[[nodiscard]] std::vector<std::size_t> make_work_order(
    const Manifest& manifest, SchedulePolicy policy, std::uint32_t partitions);

[[nodiscard]] std::vector<std::size_t> make_work_order_hybrid(
    const Manifest& manifest, SchedulePolicy primary, SchedulePolicy secondary,
    std::uint32_t weight, std::uint32_t partitions);

}
