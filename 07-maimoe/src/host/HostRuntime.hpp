#pragma once

#include "TuneConfig.hpp"

#include <filesystem>

namespace maimoe::host {

int run(const std::filesystem::path& dataset_dir,
        const std::filesystem::path& output_dir,
        const std::filesystem::path& worker_executable,
        const TuneConfig& config);

}
