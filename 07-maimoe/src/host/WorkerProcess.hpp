#pragma once

#include "maimoe/kernel_api.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

namespace maimoe::host {

class WorkerProcess {
public:
    explicit WorkerProcess(const std::filesystem::path& executable,
                           std::chrono::milliseconds timeout,
                           int core_index = -1);
    WorkerProcess(const WorkerProcess&) = delete;
    WorkerProcess& operator=(const WorkerProcess&) = delete;
    WorkerProcess(WorkerProcess&&) = delete;
    WorkerProcess& operator=(WorkerProcess&&) = delete;
    ~WorkerProcess();

    void process(std::uint64_t chart_id, std::string_view chart_text,
                 std::array<std::uint8_t, kernel::kEncodedChartBytes>& output);
    void abort() noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}
