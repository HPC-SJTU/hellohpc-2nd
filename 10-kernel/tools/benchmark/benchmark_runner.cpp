// Shared device-event timing engine. This file is included by both variant
// runners after their ACL runtime and command-line types are defined.
#ifndef RSM_BENCHMARK_RUNNER_CPP
#define RSM_BENCHMARK_RUNNER_CPP

struct TimingStats {
    uint32_t repetitions = 0;
    uint32_t conditioning_repetitions = 0;
    uint32_t fresh_slots = 0;
    float conditioning_ms = 0;
    float fresh_probe_ms = 0;
    float fresh_probe_to_median_ratio = 0;
    std::vector<float> samples_ms;
    std::vector<uint32_t> group_repetitions;
    float median_ms = 0;
    float p10_ms = 0;
    float p90_ms = 0;
    float mean_ms = 0;
    float stddev_ms = 0;
    float cv = 0;
};

float Quantile(const std::vector<float> &sorted, float q)
{
    if (sorted.size() == 1) return sorted.front();
    const float position = q * static_cast<float>(sorted.size() - 1);
    const size_t lower = static_cast<size_t>(position);
    const size_t upper = std::min(lower + 1, sorted.size() - 1);
    const float fraction = position - static_cast<float>(lower);
    return sorted[lower] * (1.0f - fraction) + sorted[upper] * fraction;
}

template <typename Prepare, typename Launch>
float TimeBatch(aclrtStream stream, uint32_t launches, Prepare &&prepare,
    Launch &&launch)
{
    // Preparation is intentionally outside the measured interval. The same buffers
    // are reused during timing; formal correctness gates validate fresh inputs.
    aclrtEvent start = nullptr;
    aclrtEvent end = nullptr;
    Check(aclrtCreateEvent(&start), "aclrtCreateEvent(start)");
    try {
        Check(aclrtCreateEvent(&end), "aclrtCreateEvent(end)");
        Check(aclrtRecordEvent(start, stream), "aclrtRecordEvent(start)");
        for (uint32_t i = 0; i < launches; ++i) launch(i);
        Check(aclrtRecordEvent(end, stream), "aclrtRecordEvent(end)");
        Check(aclrtSynchronizeEvent(end), "aclrtSynchronizeEvent(end)");
        float elapsed_ms = 0.0f;
        Check(aclrtEventElapsedTime(&elapsed_ms, start, end), "aclrtEventElapsedTime");
        aclrtDestroyEvent(end);
        aclrtDestroyEvent(start);
        return elapsed_ms;
    } catch (...) {
        if (end != nullptr) aclrtDestroyEvent(end);
        aclrtDestroyEvent(start);
        throw;
    }
}

uint32_t RoundUpToSlots(uint32_t value, uint32_t slots)
{
    const uint64_t rounded =
        ((static_cast<uint64_t>(std::max(value, 1U)) + slots - 1U) / slots) * slots;
    const uint32_t capped_multiple = (200000U / slots) * slots;
    return static_cast<uint32_t>(std::min<uint64_t>(rounded, capped_multiple));
}

template <typename Prepare, typename Launch>
float TimeLaunches(aclrtStream stream, uint32_t repetitions, uint32_t,
    Prepare &&prepare, Launch &&launch)
{
    // Keep each timing interval continuous so short kernels run as sustained
    // device work instead of isolated launches separated by host syncs.
    return repetitions == 0
        ? 0.0f
        : TimeBatch(stream, repetitions, prepare, launch);
}

template <typename Prepare, typename Launch>
TimingStats Benchmark(const Arguments &args, aclrtStream stream, uint32_t fresh_slots,
    Prepare &&prepare, Launch &&launch)
{
    if (fresh_slots == 0U) {
        throw std::runtime_error("benchmark requires one output slot");
    }
    const uint32_t warmup = RoundUpToSlots(std::max(args.warmup, 20U), fresh_slots);
    TimeLaunches(stream, warmup, fresh_slots, prepare, launch);

    uint32_t repetitions = args.fixed_repetitions;
    const uint32_t probe_repetitions = fresh_slots;
    const float probe_ms = TimeLaunches(
        stream, probe_repetitions, fresh_slots, prepare, launch);
    const float estimated_ms = std::max(probe_ms / probe_repetitions, 0.0001f);
    const uint32_t repetition_cap = (200000U / fresh_slots) * fresh_slots;
    if (repetitions == 0) {
        // The probe is a short batch and systematically overestimates
        // steady-state throughput for very small kernels, so the 50% headroom
        // below only sets each group's MINIMUM launch count.  Every formal
        // group then accumulates launches until it reaches the target duration
        // on its own: a group that speeds up after the probe is extended in
        // place, which completes the same measurement instead of failing the
        // protocol or re-selecting one.
        repetitions = RoundUpToSlots(std::clamp(
            static_cast<uint32_t>(std::ceil(args.target_ms * 1.50f / estimated_ms)),
            1U, 200000U), fresh_slots);
    } else {
        repetitions = RoundUpToSlots(repetitions, fresh_slots);
    }

    constexpr float kConditioningTargetMs = 500.0f;
    uint32_t conditioning_repetitions = 0;
    float conditioning_ms = 0.0f;
    while (conditioning_ms < kConditioningTargetMs &&
           conditioning_repetitions < repetition_cap) {
        const float observed_per_launch_ms = conditioning_repetitions == 0
            ? std::max(estimated_ms, 0.0001f)
            : std::max(conditioning_ms / conditioning_repetitions, 0.0001f);
        const uint32_t requested = RoundUpToSlots(
            std::max(
                static_cast<uint32_t>(std::ceil(
                    (kConditioningTargetMs - conditioning_ms) * 1.05f /
                    observed_per_launch_ms)),
                repetitions),
            fresh_slots);
        const uint32_t batch = std::min(
            requested, repetition_cap - conditioning_repetitions);
        if (batch == 0) break;
        conditioning_ms += TimeLaunches(
            stream, batch, fresh_slots, prepare, launch);
        conditioning_repetitions += batch;
    }

    TimingStats result;
    result.repetitions = repetitions;
    result.conditioning_repetitions = conditioning_repetitions;
    result.conditioning_ms = conditioning_ms;
    result.fresh_slots = fresh_slots;
    result.fresh_probe_ms = estimated_ms;
    result.samples_ms.reserve(args.groups);
    result.group_repetitions.reserve(args.groups);
    for (uint32_t group = 0; group < args.groups; ++group) {
        float cumulative_ms = 0.0f;
        uint32_t launches = 0;
        while (launches < repetitions ||
               (args.fixed_repetitions == 0 && cumulative_ms < args.target_ms)) {
            uint32_t requested = repetitions > launches ? repetitions - launches : 0U;
            if (requested == 0U) {
                const float observed_ms = std::max(cumulative_ms, 0.0001f);
                const float per_launch_ms = observed_ms / std::max(launches, 1U);
                requested = RoundUpToSlots(
                    std::max(
                        static_cast<uint32_t>(std::ceil(
                            (args.target_ms - cumulative_ms) * 1.10f /
                            std::max(per_launch_ms, 0.0001f))),
                        1U),
                    fresh_slots);
            }
            const uint32_t batch =
                std::min(requested, repetition_cap - launches);
            if (batch == 0) {
                // Repetition cap reached without meeting the target.  Record
                // the shortfall so the protocol validator fails the run
                // instead of silently reporting an underfed group.
                break;
            }
            cumulative_ms += TimeBatch(stream, batch, prepare, launch);
            launches += batch;
        }
        result.samples_ms.push_back(cumulative_ms / launches);
        result.group_repetitions.push_back(launches);
    }
    std::vector<float> sorted = result.samples_ms;
    std::sort(sorted.begin(), sorted.end());
    result.median_ms = Quantile(sorted, 0.5f);
    result.fresh_probe_to_median_ratio = estimated_ms / result.median_ms;
    result.p10_ms = Quantile(sorted, 0.1f);
    result.p90_ms = Quantile(sorted, 0.9f);
    result.mean_ms = std::accumulate(sorted.begin(), sorted.end(), 0.0f) / sorted.size();
    float squared_sum = 0.0f;
    for (float value : sorted) {
        const float delta = value - result.mean_ms;
        squared_sum += delta * delta;
    }
    result.stddev_ms = std::sqrt(squared_sum / sorted.size());
    result.cv = result.mean_ms > 0.0f ? result.stddev_ms / result.mean_ms : 0.0f;
    return result;
}

#endif
