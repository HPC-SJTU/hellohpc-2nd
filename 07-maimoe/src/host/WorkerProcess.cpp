#include "WorkerProcess.hpp"

#include "Protocol.hpp"
#include "ThreadAffinity.hpp"
#include "maimoe/types.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <linux/futex.h>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "WorkerShared.hpp"
#endif

namespace maimoe::host {
namespace {

[[nodiscard]] std::runtime_error worker_error(std::string_view message) {
    return std::runtime_error(std::string("kernel worker: ") + std::string(message));
}

#if defined(_WIN32)

[[nodiscard]] std::string windows_error(std::string_view operation) {
    return std::string(operation) + " failed with Windows error " +
           std::to_string(::GetLastError());
}

void close_handle(std::atomic<HANDLE>& slot) noexcept {
    const HANDLE handle = slot.exchange(INVALID_HANDLE_VALUE);
    if (handle != INVALID_HANDLE_VALUE && handle != nullptr) {
        static_cast<void>(::CloseHandle(handle));
    }
}

[[nodiscard]] bool read_exact(HANDLE handle, void* data, std::size_t bytes) noexcept {
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

[[nodiscard]] bool write_exact(HANDLE handle, const void* data, std::size_t bytes) noexcept {
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

#else

[[noreturn]] void child_exit(int status) noexcept {
    ::_exit(status);
}

[[noreturn]] void exec_worker(int shared_descriptor, const char* executable) noexcept {
    if (shared_descriptor != 3 && ::dup2(shared_descriptor, 3) < 0) {
        child_exit(125);
    }
    const int flags = ::fcntl(3, F_GETFD);
    if (flags < 0 || ::fcntl(3, F_SETFD, flags & ~FD_CLOEXEC) < 0) {
        child_exit(125);
    }
    if (shared_descriptor != 3) {
        static_cast<void>(::close(shared_descriptor));
    }
    char name[] = "maimoe_kernel";
    char* const arguments[] = {name, nullptr};
    char* const environment[] = {nullptr};
    ::execve(executable, arguments, environment);
    child_exit(127);
}

[[nodiscard]] bool wait_for_response(worker::SharedBlock& shared,
                                     std::atomic_int& process,
                                     std::chrono::steady_clock::time_point deadline) noexcept {
    std::atomic_ref<std::uint32_t> state(shared.state);
    while (state.load(std::memory_order_acquire) == worker::kSharedRequest) {
        const int child = process.load();
        if (child <= 0) {
            return false;
        }
        int status = 0;
        const pid_t wait_result = ::waitpid(static_cast<pid_t>(child), &status, WNOHANG);
        if (wait_result == static_cast<pid_t>(child) ||
            (wait_result < 0 && errno == ECHILD)) {
            int expected = child;
            static_cast<void>(process.compare_exchange_strong(expected, -1));
            return false;
        }
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::steady_clock::duration::zero()) {
            return false;
        }
        const auto poll_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::milliseconds(10));
        const auto interval = (std::min)(remaining, poll_interval);
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(interval);
        const auto nanoseconds =
            std::chrono::duration_cast<std::chrono::nanoseconds>(interval - seconds);
        const struct timespec timeout {
            static_cast<time_t>(seconds.count()), static_cast<long>(nanoseconds.count())};
        const long result = ::syscall(SYS_futex, &shared.state, FUTEX_WAIT,
                                      worker::kSharedRequest, &timeout, nullptr, 0);
        if (result < 0 && errno != EAGAIN && errno != EINTR && errno != ETIMEDOUT) {
            return false;
        }
    }
    return state.load(std::memory_order_acquire) == worker::kSharedResponse;
}

void wake_worker(worker::SharedBlock& shared) noexcept {
    static_cast<void>(::syscall(SYS_futex, &shared.state, FUTEX_WAKE,
                                INT_MAX, nullptr, nullptr, 0));
}

#endif

}

struct WorkerProcess::State {
    explicit State(std::chrono::milliseconds worker_timeout, int core_index)
        : timeout(worker_timeout), core(core_index) {}

    std::chrono::milliseconds timeout;
    int core = -1;
    std::atomic_bool aborted{false};
#if defined(_WIN32)
    std::atomic<HANDLE> process{INVALID_HANDLE_VALUE};
    std::atomic<HANDLE> input{INVALID_HANDLE_VALUE};
    std::atomic<HANDLE> output{INVALID_HANDLE_VALUE};
    std::mutex watchdog_mutex;
    std::condition_variable watchdog_cv;
    std::chrono::steady_clock::time_point deadline{};
    bool armed = false;
    bool stop_watchdog = false;
    std::jthread watchdog;
#else
    std::atomic_int process{-1};
    worker::SharedBlock* shared = nullptr;
#endif
};

WorkerProcess::WorkerProcess(const std::filesystem::path& executable,
                             std::chrono::milliseconds timeout,
                             int core_index)
    : state_(std::make_unique<State>(timeout, core_index)) {
    if (timeout <= std::chrono::milliseconds::zero()) {
        throw worker_error("timeout must be positive");
    }
#if defined(_WIN32)
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE child_input = INVALID_HANDLE_VALUE;
    HANDLE parent_input = INVALID_HANDLE_VALUE;
    HANDLE parent_output = INVALID_HANDLE_VALUE;
    HANDLE child_output = INVALID_HANDLE_VALUE;
    if (::CreatePipe(&child_input, &parent_input, &attributes, 0U) == FALSE ||
        ::SetHandleInformation(parent_input, HANDLE_FLAG_INHERIT, 0U) == FALSE ||
        ::CreatePipe(&parent_output, &child_output, &attributes, 0U) == FALSE ||
        ::SetHandleInformation(parent_output, HANDLE_FLAG_INHERIT, 0U) == FALSE) {
        if (child_input != INVALID_HANDLE_VALUE) {
            static_cast<void>(::CloseHandle(child_input));
        }
        if (parent_input != INVALID_HANDLE_VALUE) {
            static_cast<void>(::CloseHandle(parent_input));
        }
        if (parent_output != INVALID_HANDLE_VALUE) {
            static_cast<void>(::CloseHandle(parent_output));
        }
        if (child_output != INVALID_HANDLE_VALUE) {
            static_cast<void>(::CloseHandle(child_output));
        }
        throw worker_error(windows_error("CreatePipe"));
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = child_input;
    startup.hStdOutput = child_output;
    startup.hStdError = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION information{};
    std::wstring command = L"\"" + executable.wstring() + L"\"";
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    const BOOL created = ::CreateProcessW(
        executable.c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr,
        &startup, &information);
    static_cast<void>(::CloseHandle(child_input));
    static_cast<void>(::CloseHandle(child_output));
    if (created == FALSE) {
        static_cast<void>(::CloseHandle(parent_input));
        static_cast<void>(::CloseHandle(parent_output));
        throw worker_error(windows_error("CreateProcessW"));
    }
    pin_windows_process(information.hProcess, state_->core);
    static_cast<void>(::CloseHandle(information.hThread));
    state_->process.store(information.hProcess);
    state_->input.store(parent_input);
    state_->output.store(parent_output);
    state_->watchdog = std::jthread([state = state_.get()] {
        std::unique_lock lock(state->watchdog_mutex);
        while (!state->stop_watchdog) {
            state->watchdog_cv.wait(lock, [&] {
                return state->stop_watchdog || state->armed;
            });
            if (state->stop_watchdog) {
                break;
            }
            const auto deadline = state->deadline;
            if (state->watchdog_cv.wait_until(lock, deadline, [&] {
                    return state->stop_watchdog || !state->armed || state->deadline != deadline;
                })) {
                continue;
            }
            lock.unlock();
            state->aborted.store(true);
            const HANDLE process = state->process.load();
            if (process != INVALID_HANDLE_VALUE) {
                static_cast<void>(::TerminateProcess(process, 124U));
            }
            close_handle(state->input);
            close_handle(state->output);
            lock.lock();
            state->armed = false;
        }
    });
#else
    const std::string executable_string = executable.string();
    const int shared_descriptor = static_cast<int>(
        ::syscall(SYS_memfd_create, "maimoe-worker", MFD_CLOEXEC));
    if (shared_descriptor < 0 ||
        ::ftruncate(shared_descriptor, static_cast<off_t>(sizeof(worker::SharedBlock))) != 0) {
        if (shared_descriptor >= 0) {
            static_cast<void>(::close(shared_descriptor));
        }
        throw worker_error("cannot create shared worker memory");
    }
    void* const mapping = ::mmap(nullptr, sizeof(worker::SharedBlock),
                                 PROT_READ | PROT_WRITE, MAP_SHARED, shared_descriptor, 0);
    if (mapping == MAP_FAILED) {
        static_cast<void>(::close(shared_descriptor));
        throw worker_error("cannot map shared worker memory");
    }
    state_->shared = new (mapping) worker::SharedBlock();
    const pid_t child = ::fork();
    if (child < 0) {
        static_cast<void>(::munmap(mapping, sizeof(worker::SharedBlock)));
        static_cast<void>(::close(shared_descriptor));
        state_->shared = nullptr;
        throw worker_error("cannot fork worker");
    }
    if (child == 0) {
        pin_current(state_->core);
        exec_worker(shared_descriptor, executable_string.c_str());
    }
    static_cast<void>(::close(shared_descriptor));
    state_->process.store(static_cast<int>(child));
#endif
}

WorkerProcess::~WorkerProcess() {
    abort();
#if defined(_WIN32)
    {
        std::lock_guard lock(state_->watchdog_mutex);
        state_->stop_watchdog = true;
        state_->armed = false;
    }
    state_->watchdog_cv.notify_all();
    if (state_->watchdog.joinable()) {
        state_->watchdog.join();
    }
    const HANDLE process = state_->process.exchange(INVALID_HANDLE_VALUE);
    if (process != INVALID_HANDLE_VALUE) {
        static_cast<void>(::WaitForSingleObject(process, 5000U));
        static_cast<void>(::CloseHandle(process));
    }
#else
    const int child = state_->process.exchange(-1);
    if (child > 0) {
        int status = 0;
        while (::waitpid(static_cast<pid_t>(child), &status, 0) < 0 && errno == EINTR) {
        }
    }
    if (state_->shared != nullptr) {
        static_cast<void>(::munmap(state_->shared, sizeof(worker::SharedBlock)));
        state_->shared = nullptr;
    }
#endif
}

void WorkerProcess::abort() noexcept {
    if (state_ == nullptr || state_->aborted.exchange(true)) {
        return;
    }
#if defined(_WIN32)
    const HANDLE process = state_->process.load();
    if (process != INVALID_HANDLE_VALUE) {
        static_cast<void>(::TerminateProcess(process, 124U));
    }
    close_handle(state_->input);
    close_handle(state_->output);
#else
    const int child = state_->process.load();
    if (child > 0) {
        static_cast<void>(::kill(static_cast<pid_t>(child), SIGKILL));
    }
#endif
}

void WorkerProcess::process(
    std::uint64_t chart_id, std::string_view chart_text,
    std::array<std::uint8_t, kernel::kEncodedChartBytes>& output) {
    if (state_->aborted.load()) {
        throw worker_error("worker is unavailable");
    }
    if (chart_id == 0U ||
        chart_id > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) ||
        chart_text.size() > maimoe::kMaxChartBytes) {
        throw worker_error("invalid chart request");
    }
    std::array<std::uint8_t, worker::kHeaderBytes> response{};
#if defined(_WIN32)
    const auto request = worker::request_header(chart_id, chart_text.size());
    {
        std::lock_guard lock(state_->watchdog_mutex);
        state_->deadline = std::chrono::steady_clock::now() + state_->timeout;
        state_->armed = true;
    }
    state_->watchdog_cv.notify_all();
    const HANDLE input = state_->input.load();
    const HANDLE worker_output = state_->output.load();
    const bool ok = input != INVALID_HANDLE_VALUE && worker_output != INVALID_HANDLE_VALUE &&
                    write_exact(input, request.data(), request.size()) &&
                    write_exact(input, chart_text.data(), chart_text.size()) &&
                    read_exact(worker_output, response.data(), response.size());
#else
    worker::SharedBlock& shared = *state_->shared;
    shared.request_magic = worker::kRequestMagic;
    shared.request_chart_id = chart_id;
    shared.request_text_bytes = chart_text.size();
    std::copy(chart_text.begin(), chart_text.end(), shared.text.begin());
    std::atomic_ref<std::uint32_t> shared_state(shared.state);
    shared_state.store(worker::kSharedRequest, std::memory_order_release);
    wake_worker(shared);
    const auto deadline = std::chrono::steady_clock::now() + state_->timeout;
    const bool ok = wait_for_response(shared, state_->process, deadline);
    if (ok) {
        worker::store_u64(response, 0U, shared.response_magic);
        worker::store_u64(response, 8U, shared.response_chart_id);
        worker::store_u64(response, 16U, shared.response_status);
    }
#endif
    if (!ok) {
        abort();
        throw worker_error("worker crashed, hung, or stopped its protocol transport");
    }
    if (worker::load_u64(response, 0U) != worker::kResponseMagic ||
        worker::load_u64(response, 8U) != chart_id ||
        worker::load_u64(response, 16U) > 1U) {
        abort();
        throw worker_error("worker returned an invalid protocol response");
    }
    if (worker::load_u64(response, 16U) != 0U) {
        abort();
        throw worker_error("participant kernel threw an exception");
    }
#if defined(_WIN32)
    const bool payload_ok = read_exact(worker_output, output.data(), output.size());
    {
        std::lock_guard lock(state_->watchdog_mutex);
        state_->armed = false;
    }
    state_->watchdog_cv.notify_all();
#else
    std::copy(state_->shared->output.begin(), state_->shared->output.end(), output.begin());
    std::atomic_ref<std::uint32_t>(state_->shared->state).store(
        worker::kSharedIdle, std::memory_order_release);
    wake_worker(*state_->shared);
    const bool payload_ok = true;
#endif
    if (!payload_ok) {
        abort();
        throw worker_error("worker returned a partial encoded chart");
    }
}

}
