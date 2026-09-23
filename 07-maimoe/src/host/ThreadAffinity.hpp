#pragma once


#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sched.h>
#include <unistd.h>
#endif

namespace maimoe::host {

inline int allowed_core_count() noexcept {
#if defined(_WIN32)
    DWORD_PTR process_mask = 0;
    DWORD_PTR system_mask = 0;
    if (::GetProcessAffinityMask(::GetCurrentProcess(), &process_mask,
                                 &system_mask) == FALSE ||
        process_mask == 0) {
        return 1;
    }
    int count = 0;
    DWORD_PTR mask = process_mask;
    while (mask != 0) {
        count += static_cast<int>(mask & static_cast<DWORD_PTR>(1U));
        mask >>= 1U;
    }
    return count > 0 ? count : 1;
#else
    cpu_set_t set;
    CPU_ZERO(&set);
    if (::sched_getaffinity(0, sizeof(set), &set) != 0) {
        const long count = ::sysconf(_SC_NPROCESSORS_ONLN);
        return count > 0 ? static_cast<int>(count) : 1;
    }
    const int count = CPU_COUNT(&set);
    return count > 0 ? count : 1;
#endif
}

inline void pin_current(int core_index) noexcept {
    if (core_index < 0) {
        return;
    }
    const int count = allowed_core_count();
    if (count <= 0) {
        return;
    }
    const int target = core_index % count;
#if defined(_WIN32)
    DWORD_PTR process_mask = 0;
    DWORD_PTR system_mask = 0;
    if (::GetProcessAffinityMask(::GetCurrentProcess(), &process_mask,
                                 &system_mask) == FALSE ||
        process_mask == 0) {
        return;
    }
    int seen = 0;
    DWORD_PTR mask = 0;
    for (int bit = 0; bit < static_cast<int>(sizeof(DWORD_PTR) * 8); ++bit) {
        const DWORD_PTR candidate =
            static_cast<DWORD_PTR>(1U) << static_cast<unsigned int>(bit);
        if ((process_mask & candidate) == 0) {
            continue;
        }
        if (seen == target) {
            mask = candidate;
            break;
        }
        ++seen;
    }
    if (mask != 0) {
        static_cast<void>(::SetThreadAffinityMask(::GetCurrentThread(), mask));
    }
#else
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (::sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
        return;
    }
    int seen = 0;
    int chosen = -1;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (!CPU_ISSET(cpu, &allowed)) {
            continue;
        }
        if (seen == target) {
            chosen = cpu;
            break;
        }
        ++seen;
    }
    if (chosen < 0) {
        return;
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(chosen, &set);
    static_cast<void>(::sched_setaffinity(0, sizeof(set), &set));
#endif
}

#if defined(_WIN32)
inline void pin_windows_process(::HANDLE process, int core_index) noexcept {
    if (core_index < 0 || process == nullptr || process == INVALID_HANDLE_VALUE) {
        return;
    }
    const int count = allowed_core_count();
    if (count <= 0) {
        return;
    }
    const int target = core_index % count;
    DWORD_PTR process_mask = 0;
    DWORD_PTR system_mask = 0;
    if (::GetProcessAffinityMask(process, &process_mask, &system_mask) == FALSE ||
        process_mask == 0) {
        return;
    }
    int seen = 0;
    DWORD_PTR mask = 0;
    for (int bit = 0; bit < static_cast<int>(sizeof(DWORD_PTR) * 8); ++bit) {
        const DWORD_PTR candidate =
            static_cast<DWORD_PTR>(1U) << static_cast<unsigned int>(bit);
        if ((process_mask & candidate) == 0) {
            continue;
        }
        if (seen == target) {
            mask = candidate;
            break;
        }
        ++seen;
    }
    if (mask != 0) {
        static_cast<void>(::SetProcessAffinityMask(process, mask));
    }
}
#endif

}
