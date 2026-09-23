#include "Protocol.hpp"
#include "WorkerShared.hpp"

#include "maimoe/kernel_api.hpp"
#include "maimoe/types.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>

#if defined(__linux__)
#include <cerrno>
#include <climits>
#include <linux/futex.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

#if defined(__linux__)
maimoe::worker::SharedBlock* g_shared_block = nullptr;
#endif

#if defined(_WIN32)
using IoHandle = HANDLE;

[[nodiscard]] bool read_exact(IoHandle handle, void* data, std::size_t bytes) noexcept {
    auto* output = static_cast<unsigned char*>(data);
    while (bytes > 0U) {
        DWORD count = 0U;
        const DWORD request = static_cast<DWORD>(
            (std::min)(bytes, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        if (::ReadFile(handle, output, request, &count, nullptr) == FALSE || count == 0U) {
            return false;
        }
        output += count;
        bytes -= count;
    }
    return true;
}

[[nodiscard]] bool write_exact(IoHandle handle, const void* data, std::size_t bytes) noexcept {
    const auto* input = static_cast<const unsigned char*>(data);
    while (bytes > 0U) {
        DWORD count = 0U;
        const DWORD request = static_cast<DWORD>(
            (std::min)(bytes, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
        if (::WriteFile(handle, input, request, &count, nullptr) == FALSE || count == 0U) {
            return false;
        }
        input += count;
        bytes -= count;
    }
    return true;
}
#endif

int run_worker() {
#if defined(_WIN32)
    const IoHandle input = ::GetStdHandle(STD_INPUT_HANDLE);
    const IoHandle output = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (input == nullptr || input == INVALID_HANDLE_VALUE ||
        output == nullptr || output == INVALID_HANDLE_VALUE) {
        return 2;
    }
    while (true) {
        std::array<std::uint8_t, maimoe::worker::kHeaderBytes> request{};
        if (!read_exact(input, request.data(), request.size())) {
            return 0;
        }
        const std::uint64_t magic = maimoe::worker::load_u64(request, 0U);
        const std::uint64_t chart_id = maimoe::worker::load_u64(request, 8U);
        const std::uint64_t text_bytes = maimoe::worker::load_u64(request, 16U);
        if (magic != maimoe::worker::kRequestMagic || chart_id == 0U ||
            chart_id > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) ||
            text_bytes > maimoe::kMaxChartBytes) {
            return 3;
        }
        std::string text(static_cast<std::size_t>(text_bytes), '\0');
        if (!read_exact(input, text.data(), text.size())) {
            return 4;
        }
        std::array<std::uint8_t, maimoe::kernel::kEncodedChartBytes> encoded{};
        std::uint64_t status = 0U;
        try {
            maimoe::kernel::process_chart(chart_id, text, encoded);
        } catch (...) {
            status = 1U;
        }
        const auto response = maimoe::worker::response_header(chart_id, status);
        if (!write_exact(output, response.data(), response.size())) {
            return 5;
        }
        if (status == 0U && !write_exact(output, encoded.data(), encoded.size())) {
            return 6;
        }
    }
#else
    maimoe::worker::initialize_shared_block();
    maimoe::worker::SharedBlock& shared = maimoe::worker::shared_block();
    std::atomic_ref<std::uint32_t> state(shared.state);
    while (true) {
        std::uint32_t current = state.load(std::memory_order_acquire);
        while (current == maimoe::worker::kSharedIdle ||
               current == maimoe::worker::kSharedResponse) {
            maimoe::worker::wait_shared_state(current);
            current = state.load(std::memory_order_acquire);
        }
        if (current == maimoe::worker::kSharedStop) {
            return 0;
        }
        if (current != maimoe::worker::kSharedRequest ||
            shared.request_magic != maimoe::worker::kRequestMagic ||
            shared.request_chart_id == 0U ||
            shared.request_chart_id >
                static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) ||
            shared.request_text_bytes > maimoe::kMaxChartBytes) {
            return 3;
        }
        std::uint64_t status = 0U;
        try {
            const std::string_view text(
                shared.text.data(), static_cast<std::size_t>(shared.request_text_bytes));
            maimoe::kernel::process_chart(shared.request_chart_id, text, shared.output);
        } catch (...) {
            status = 1U;
        }
        shared.response_magic = maimoe::worker::kResponseMagic;
        shared.response_chart_id = shared.request_chart_id;
        shared.response_status = status;
        state.store(maimoe::worker::kSharedResponse, std::memory_order_release);
        maimoe::worker::wake_shared_state();
    }
#endif
}

}

#if defined(__linux__)
namespace maimoe::worker {

void initialize_shared_block() {
    void* const mapping = ::mmap(nullptr, sizeof(SharedBlock), PROT_READ | PROT_WRITE,
                                 MAP_SHARED, 3, 0);
    if (mapping == MAP_FAILED) {
        throw std::runtime_error("cannot map Host worker protocol");
    }
    g_shared_block = static_cast<SharedBlock*>(mapping);
    static_cast<void>(::close(3));
}

SharedBlock& shared_block() noexcept {
    if (g_shared_block == nullptr) {
        std::terminate();
    }
    return *g_shared_block;
}

void wait_shared_state(std::uint32_t expected) noexcept {
    static_cast<void>(::syscall(SYS_futex, &shared_block().state,
                                FUTEX_WAIT, expected, nullptr, nullptr, 0));
}

void wake_shared_state() noexcept {
    static_cast<void>(::syscall(SYS_futex, &shared_block().state,
                                FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0));
}

}
#endif

int main() {
    return run_worker();
}
