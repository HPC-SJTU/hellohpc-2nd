#pragma once

#include <acl/acl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace RsmRunner {

#ifndef RSM_WORKSPACE_BYTES_WITH_OFFSETS
#define RSM_WORKSPACE_BYTES_WITH_OFFSETS(n, d, s, offsets) \
    RSM_WORKSPACE_BYTES(n, d, s)
#endif

#ifndef RSM_CONFIGURE_LAUNCH_WITH_OFFSETS
#define RSM_CONFIGURE_LAUNCH_WITH_OFFSETS(n, d, s, epsilon, offsets, tiling, block, key) \
    ConfigureLaunch(n, d, s, epsilon, tiling, block, key)
#endif

#ifndef RSM_BUILD_WORKSPACE_IMAGE
#define RSM_BUILD_WORKSPACE_IMAGE(n, d, s, offsets) std::vector<uint8_t>{}
#endif

constexpr size_t kGuardBytes = 256;
constexpr uint8_t kGuardValue = 0xa5;
constexpr size_t kMaxWorkspaceBytes = 4 * 1024 * 1024;
constexpr float kMinimumEpsilon = 3.0e-6f;
constexpr float kMaximumEpsilon = 1.0e-2f;

void Check(aclError status, std::string_view operation)
{
    if (status != ACL_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with ACL error " +
            std::to_string(status));
    }
}

struct Arguments {
    std::filesystem::path input_dir;
    std::filesystem::path output_dir;
    std::filesystem::path benchmark_json;
    std::filesystem::path workspace_output;
    std::filesystem::path sequence_file;
    std::filesystem::path reuse_input_dir;
    uint32_t n = 0;
    uint32_t d = 0;
    uint32_t s = 0;
    float epsilon = 1.0e-5f;
    int device = 0;
    uint32_t warmup = 20;
    uint32_t groups = 0;
    uint32_t robustness_runs = 1;
    uint32_t output_fill_byte = 0xff;
    uint32_t workspace_fill_byte = 0x5a;
    float target_ms = 100.0f;
    uint32_t fixed_repetitions = 0;
    std::string timing_mode = "kernel";
    bool mutate_input_after_launch = false;
    bool worker_mode = false;
};

uint32_t ParseU32(const std::string &value, const char *name)
{
    size_t consumed = 0;
    const unsigned long parsed = std::stoul(value, &consumed);
    if (consumed != value.size() || parsed > UINT32_MAX) {
        throw std::runtime_error(std::string("invalid ") + name + ": " + value);
    }
    return static_cast<uint32_t>(parsed);
}

Arguments ParseArguments(int argc, char **argv)
{
    Arguments args;
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        if (i + 1 >= argc) {
            throw std::runtime_error("missing value for " + option);
        }
        const std::string value = argv[++i];
        if (option == "--input-dir") args.input_dir = value;
        else if (option == "--output-dir") args.output_dir = value;
        else if (option == "--benchmark-json") args.benchmark_json = value;
        else if (option == "--workspace-output") args.workspace_output = value;
        else if (option == "--sequence-file") args.sequence_file = value;
        else if (option == "--reuse-input-dir") args.reuse_input_dir = value;
        else if (option == "--n") args.n = ParseU32(value, "N");
        else if (option == "--d") args.d = ParseU32(value, "D");
        else if (option == "--s") args.s = ParseU32(value, "S");
        else if (option == "--epsilon") args.epsilon = std::stof(value);
        else if (option == "--device") args.device = static_cast<int>(ParseU32(value, "device"));
        else if (option == "--warmup") args.warmup = ParseU32(value, "warmup");
        else if (option == "--groups") args.groups = ParseU32(value, "groups");
        else if (option == "--robustness-runs") {
            args.robustness_runs = ParseU32(value, "robustness-runs");
        }
        else if (option == "--output-fill-byte") {
            args.output_fill_byte = ParseU32(value, "output-fill-byte");
        }
        else if (option == "--workspace-fill-byte") {
            args.workspace_fill_byte = ParseU32(value, "workspace-fill-byte");
        }
        else if (option == "--target-ms") args.target_ms = std::stof(value);
        else if (option == "--fixed-repetitions") {
            args.fixed_repetitions = ParseU32(value, "fixed-repetitions");
        }
        else if (option == "--timing-mode") args.timing_mode = value;
        else if (option == "--mutate-input-after-launch") {
            args.mutate_input_after_launch = ParseU32(value, "mutate-input-after-launch") != 0;
        }
        else if (option == "--worker-mode") {
            args.worker_mode = ParseU32(value, "worker-mode") != 0;
        }
        else throw std::runtime_error("unknown option: " + option);
    }
    if (!args.worker_mode && args.sequence_file.empty() &&
        (args.input_dir.empty() || args.output_dir.empty() ||
         args.n == 0 || args.d == 0 || args.s == 0)) {
        throw std::runtime_error(
            "required: --input-dir DIR --output-dir DIR --n N --d D --s S");
    }
    if (!std::isfinite(args.epsilon) || args.epsilon < kMinimumEpsilon ||
        args.epsilon > kMaximumEpsilon || args.d > 256 || args.groups > 100 ||
        args.robustness_runs == 0 || args.robustness_runs > 16 ||
        args.output_fill_byte > UINT8_MAX || args.workspace_fill_byte > UINT8_MAX ||
        args.fixed_repetitions > 200000 ||
        (args.groups > 0 && args.robustness_runs != 1) ||
        (args.timing_mode != "kernel" && args.timing_mode != "empty") ||
        (args.mutate_input_after_launch &&
         (args.groups > 0 || args.timing_mode != "kernel")) ||
        (!args.reuse_input_dir.empty() &&
         (args.groups > 0 || args.robustness_runs != 1 ||
          args.timing_mode != "kernel" || !args.sequence_file.empty())) ||
        (args.worker_mode &&
         (!args.sequence_file.empty() || !args.reuse_input_dir.empty() ||
          args.robustness_runs != 1 || args.mutate_input_after_launch))) {
        throw std::runtime_error("invalid epsilon, D, or group count");
    }
    return args;
}

template <typename T>
std::vector<T> ReadBinary(const std::filesystem::path &path, size_t count)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("cannot open " + path.string());
    const size_t expected = count * sizeof(T);
    if (static_cast<size_t>(stream.tellg()) != expected) {
        throw std::runtime_error("unexpected byte count in " + path.string());
    }
    stream.seekg(0);
    std::vector<T> values(count);
    stream.read(reinterpret_cast<char *>(values.data()), static_cast<std::streamsize>(expected));
    if (!stream) throw std::runtime_error("cannot read " + path.string());
    return values;
}

template <typename T>
void WriteBinary(const std::filesystem::path &path, const std::vector<T> &values)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) throw std::runtime_error("cannot create " + path.string());
    stream.write(reinterpret_cast<const char *>(values.data()),
        static_cast<std::streamsize>(values.size() * sizeof(T)));
    if (!stream) throw std::runtime_error("cannot write " + path.string());
}

class DeviceBuffer {
public:
    explicit DeviceBuffer(size_t payload_bytes) : payload_bytes_(payload_bytes)
    {
        Check(aclrtMalloc(&base_, payload_bytes + 2 * kGuardBytes,
            ACL_MEM_MALLOC_HUGE_FIRST), "aclrtMalloc");
        std::vector<uint8_t> guard(kGuardBytes, kGuardValue);
        Check(aclrtMemcpy(base_, kGuardBytes, guard.data(), kGuardBytes,
            ACL_MEMCPY_HOST_TO_DEVICE), "copy leading guard");
        Check(aclrtMemcpy(static_cast<uint8_t *>(base_) + kGuardBytes + payload_bytes_,
            kGuardBytes, guard.data(), kGuardBytes, ACL_MEMCPY_HOST_TO_DEVICE),
            "copy trailing guard");
    }

    DeviceBuffer(const DeviceBuffer &) = delete;
    DeviceBuffer &operator=(const DeviceBuffer &) = delete;
    ~DeviceBuffer()
    {
        if (base_ != nullptr) aclrtFree(base_);
    }

    void *payload() const { return static_cast<uint8_t *>(base_) + kGuardBytes; }
    size_t size() const { return payload_bytes_; }

    void Upload(const void *source)
    {
        Check(aclrtMemcpy(payload(), payload_bytes_, source, payload_bytes_,
            ACL_MEMCPY_HOST_TO_DEVICE), "upload payload");
    }

    void Download(void *destination) const
    {
        Check(aclrtMemcpy(destination, payload_bytes_, payload(), payload_bytes_,
            ACL_MEMCPY_DEVICE_TO_HOST), "download payload");
    }

    void FillAsync(uint8_t value, aclrtStream stream)
    {
        Check(aclrtMemsetAsync(payload(), payload_bytes_, value, payload_bytes_, stream),
            "fill payload");
    }

    void CheckGuards(std::string_view name) const
    {
        std::array<uint8_t, kGuardBytes> leading{};
        std::array<uint8_t, kGuardBytes> trailing{};
        Check(aclrtMemcpy(leading.data(), leading.size(), base_, leading.size(),
            ACL_MEMCPY_DEVICE_TO_HOST), "read leading guard");
        Check(aclrtMemcpy(trailing.data(), trailing.size(),
            static_cast<uint8_t *>(base_) + kGuardBytes + payload_bytes_, trailing.size(),
            ACL_MEMCPY_DEVICE_TO_HOST), "read trailing guard");
        const auto valid = [](const auto &guard) {
            return std::all_of(guard.begin(), guard.end(),
                [](uint8_t value) { return value == kGuardValue; });
        };
        if (!valid(leading) || !valid(trailing)) {
            throw std::runtime_error(std::string(name) + " device guard was modified");
        }
    }

private:
    void *base_ = nullptr;
    size_t payload_bytes_ = 0;
};

class Runtime {
public:
    explicit Runtime(int device) : device_(device)
    {
        Check(aclInit(nullptr), "aclInit");
        initialized_ = true;
        Check(aclrtSetDevice(device_), "aclrtSetDevice");
        device_set_ = true;
        Check(aclrtCreateStream(&stream_), "aclrtCreateStream");
    }

    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;
    ~Runtime()
    {
        if (stream_ != nullptr) aclrtDestroyStream(stream_);
        if (device_set_) aclrtResetDevice(device_);
        if (initialized_) aclFinalize();
    }
    aclrtStream stream() const { return stream_; }

private:
    int device_ = 0;
    bool initialized_ = false;
    bool device_set_ = false;
    aclrtStream stream_ = nullptr;
};

#include "../../benchmark/benchmark_runner.cpp"

void WriteTimingJson(const std::filesystem::path &path, const Arguments &args,
    uint32_t block_dim, uint32_t tiling_key, uint32_t verification_slot,
    const TimingStats &stats)
{
    std::ofstream stream(path, std::ios::trunc);
    if (!stream) throw std::runtime_error("cannot create " + path.string());
    stream << std::setprecision(9)
           << "{\n"
           << "  \"variant\": \"" RSM_VARIANT "\",\n"
           << "  \"N\": " << args.n << ",\n"
           << "  \"D\": " << args.d << ",\n"
           << "  \"S\": " << args.s << ",\n"
           << "  \"block_dim\": " << block_dim << ",\n"
           << "  \"tiling_key\": " << tiling_key << ",\n"
           << "  \"timing_mode\": \"" << args.timing_mode << "\",\n"
           << "  \"warmup\": " << std::max(args.warmup, 20U) << ",\n"
           << "  \"conditioning_repetitions\": "
           << stats.conditioning_repetitions << ",\n"
           << "  \"conditioning_ms\": " << stats.conditioning_ms << ",\n"
           << "  \"groups\": " << stats.samples_ms.size() << ",\n"
           << "  \"repetitions_per_group\": " << stats.repetitions << ",\n"
           << "  \"fresh_slots\": " << stats.fresh_slots << ",\n"
           << "  \"verification_slot\": " << verification_slot << ",\n"
           << "  \"fresh_state_per_launch\": false,\n"
           << "  \"fresh_probe_ms\": " << stats.fresh_probe_ms << ",\n"
           << "  \"fresh_probe_to_median_ratio\": "
           << stats.fresh_probe_to_median_ratio << ",\n"
           << "  \"target_group_ms\": " << args.target_ms << ",\n"
           << "  \"median_ms\": " << stats.median_ms << ",\n"
           << "  \"p10_ms\": " << stats.p10_ms << ",\n"
           << "  \"p90_ms\": " << stats.p90_ms << ",\n"
           << "  \"mean_ms\": " << stats.mean_ms << ",\n"
           << "  \"stddev_ms\": " << stats.stddev_ms << ",\n"
           << "  \"cv\": " << stats.cv << ",\n"
           << "  \"group_repetitions\": [";
    for (size_t i = 0; i < stats.group_repetitions.size(); ++i) {
        if (i != 0) stream << ", ";
        stream << stats.group_repetitions[i];
    }
    stream << "],\n"
           << "  \"samples_ms\": [";
    for (size_t i = 0; i < stats.samples_ms.size(); ++i) {
        if (i != 0) stream << ", ";
        stream << stats.samples_ms[i];
    }
    stream << "]\n}\n";
}

int RunCase(const Arguments &args, Runtime &runtime, size_t address_shift_bytes)
{
    auto score = ReadBinary<uint16_t>(args.input_dir / "score.bin", args.n);
    auto x = ReadBinary<uint16_t>(args.input_dir / "x.bin",
        static_cast<size_t>(args.n) * args.d);
    const auto offsets = ReadBinary<int32_t>(args.input_dir / "offsets.bin", args.s + 1);
    if (offsets.front() != 0 || offsets.back() != static_cast<int32_t>(args.n)) {
        throw std::runtime_error("offset endpoints do not match N");
    }
    for (uint32_t i = 0; i < args.s; ++i) {
        if (offsets[i] >= offsets[i + 1]) throw std::runtime_error("empty or invalid segment");
    }

    std::unique_ptr<DeviceBuffer> address_shift;
    if (address_shift_bytes != 0) {
        address_shift = std::make_unique<DeviceBuffer>(address_shift_bytes);
    }
    DeviceBuffer score_device(score.size() * sizeof(score[0]));
    DeviceBuffer x_device(x.size() * sizeof(x[0]));
    DeviceBuffer offsets_device(offsets.size() * sizeof(offsets[0]));
    const size_t moment_bytes = static_cast<size_t>(args.s) * args.d * sizeof(float);
    const size_t lse_bytes = static_cast<size_t>(args.s) * sizeof(float);
    const size_t workspace_bytes = std::max<size_t>(
        RSM_WORKSPACE_BYTES_WITH_OFFSETS(
            args.n, args.d, args.s, offsets.data()), 1);
    if (workspace_bytes > kMaxWorkspaceBytes) {
        throw std::runtime_error("workspace exceeds the 4 MiB submission limit");
    }
    const auto workspace_image = RSM_BUILD_WORKSPACE_IMAGE(
        args.n, args.d, args.s, offsets.data());
    if (workspace_image.size() > workspace_bytes) {
        throw std::runtime_error("workspace metadata exceeds allocated workspace");
    }
    score_device.Upload(score.data());
    x_device.Upload(x.data());
    offsets_device.Upload(offsets.data());
    DeviceBuffer mean_device(moment_bytes);
    DeviceBuffer rstd_device(moment_bytes);
    DeviceBuffer lse_device(lse_bytes);
    DeviceBuffer workspace_device(workspace_bytes);
    constexpr uint32_t slot_count = 1;
    constexpr uint32_t verification_slot = 0;
    auto prepare = [&](uint32_t) {
        // 0xff bytes form a quiet/negative NaN on the target's IEEE FP32 format.
        mean_device.FillAsync(static_cast<uint8_t>(args.output_fill_byte), runtime.stream());
        rstd_device.FillAsync(static_cast<uint8_t>(args.output_fill_byte), runtime.stream());
        lse_device.FillAsync(static_cast<uint8_t>(args.output_fill_byte), runtime.stream());
        workspace_device.FillAsync(
            static_cast<uint8_t>(args.workspace_fill_byte), runtime.stream());
        if (!workspace_image.empty()) {
            Check(aclrtMemcpyAsync(workspace_device.payload(), workspace_bytes,
                workspace_image.data(), workspace_image.size(),
                ACL_MEMCPY_HOST_TO_DEVICE, runtime.stream()),
                "upload workspace metadata");
        }
    };
    prepare(slot_count);

    RSM_TILING_TYPE tiling{};
    uint32_t block_dim = 0;
    uint32_t tiling_key = 0;
    RSM_CONFIGURE_LAUNCH_WITH_OFFSETS(
        args.n, args.d, args.s, args.epsilon, offsets.data(),
        &tiling, &block_dim, &tiling_key);
    auto launch = [&](uint32_t) {
        if (args.timing_mode == "empty") return;
        Check(RSM_LAUNCH(block_dim, runtime.stream(), score_device.payload(),
            x_device.payload(), offsets_device.payload(), mean_device.payload(),
            rstd_device.payload(), lse_device.payload(), &tiling,
            workspace_device.payload()),
            RSM_VARIANT " kernel launch");
    };

    launch(0);
    std::vector<uint16_t> mutated_score;
    if (args.mutate_input_after_launch) {
        mutated_score.assign(score.size(), 0);
        Check(aclrtMemcpyAsync(score_device.payload(), score_device.size(),
            mutated_score.data(), mutated_score.size() * sizeof(mutated_score[0]),
            ACL_MEMCPY_HOST_TO_DEVICE, runtime.stream()),
            "enqueue input mutation after kernel");
    }
    Check(aclrtSynchronizeStream(runtime.stream()), "initial synchronization");
    if (!args.reuse_input_dir.empty()) {
        const auto original_score = score;
        const auto original_x = x;
        auto reused_score = ReadBinary<uint16_t>(
            args.reuse_input_dir / "score.bin", args.n);
        auto reused_x = ReadBinary<uint16_t>(
            args.reuse_input_dir / "x.bin", static_cast<size_t>(args.n) * args.d);
        const auto reused_offsets = ReadBinary<int32_t>(
            args.reuse_input_dir / "offsets.bin", args.s + 1);
        if (reused_offsets != offsets) {
            throw std::runtime_error("reuse input must have identical offsets");
        }

        auto snapshot = [&](uint32_t step) {
            const auto directory =
                args.output_dir / "alternating_sequence" / ("step_" + std::to_string(step));
            std::filesystem::create_directories(directory);
            std::vector<float> step_mean(static_cast<size_t>(args.s) * args.d);
            std::vector<float> step_rstd(static_cast<size_t>(args.s) * args.d);
            std::vector<float> step_lse(args.s);
            mean_device.Download(step_mean.data());
            rstd_device.Download(step_rstd.data());
            lse_device.Download(step_lse.data());
            WriteBinary(directory / "mean.bin", step_mean);
            WriteBinary(directory / "rstd.bin", step_rstd);
            WriteBinary(directory / "logsumexp.bin", step_lse);
        };
        const auto alternating_started = std::chrono::steady_clock::now();
        snapshot(0);

        // Keep the same device allocations and intentionally retain output/workspace
        // contents between launches.  Every A->B->A->B result is checked by the
        // runtime robustness harness, so output sentinels and other cross-launch
        // memoization cannot hide behind the fixed-buffer benchmark protocol.
        for (uint32_t step = 1; step < 4; ++step) {
            const bool use_reused = (step % 2U) != 0U;
            const auto &step_score = use_reused ? reused_score : original_score;
            const auto &step_x = use_reused ? reused_x : original_x;
            score_device.Upload(step_score.data());
            x_device.Upload(step_x.data());
            launch(0);
            Check(aclrtSynchronizeStream(runtime.stream()),
                "alternating-state synchronization");
            snapshot(step);
        }
        const double alternating_elapsed =
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() - alternating_started).count();
        std::ofstream timing(
            args.output_dir / "alternating_sequence" / "timing.json");
        if (!timing) {
            throw std::runtime_error("failed to write alternating-state timing");
        }
        timing << std::setprecision(9)
               << "{\n  \"elapsed_seconds\": " << alternating_elapsed << "\n}\n";
        score = std::move(reused_score);
        x = std::move(reused_x);
    }
    if (args.groups > 0) {
        const TimingStats stats = Benchmark(
            args, runtime.stream(), slot_count, prepare, launch);
        if (args.benchmark_json.empty()) {
            throw std::runtime_error("--benchmark-json is required when --groups is nonzero");
        }
        WriteTimingJson(
            args.benchmark_json, args, block_dim, tiling_key, verification_slot, stats);
        std::cout << RSM_VARIANT << " median_ms=" << stats.median_ms
                  << " cv=" << stats.cv << " repetitions=" << stats.repetitions << '\n';
    }

    std::filesystem::create_directories(args.output_dir);
    std::vector<float> mean(static_cast<size_t>(args.s) * args.d);
    std::vector<float> rstd(static_cast<size_t>(args.s) * args.d);
    std::vector<float> logsumexp(args.s);
    mean_device.Download(mean.data());
    rstd_device.Download(rstd.data());
    lse_device.Download(logsumexp.data());
    if (!args.workspace_output.empty()) {
        std::vector<uint8_t> workspace(workspace_device.size());
        workspace_device.Download(workspace.data());
        WriteBinary(args.workspace_output, workspace);
    }
    WriteBinary(args.output_dir / "mean.bin", mean);
    WriteBinary(args.output_dir / "rstd.bin", rstd);
    WriteBinary(args.output_dir / "logsumexp.bin", logsumexp);

    score_device.CheckGuards("score");
    x_device.CheckGuards("x");
    offsets_device.CheckGuards("offsets");
    mean_device.CheckGuards("mean");
    rstd_device.CheckGuards("rstd");
    lse_device.CheckGuards("logsumexp");
    workspace_device.CheckGuards("workspace");
    std::vector<uint16_t> score_after(score.size());
    std::vector<uint16_t> x_after(x.size());
    std::vector<int32_t> offsets_after(offsets.size());
    score_device.Download(score_after.data());
    x_device.Download(x_after.data());
    offsets_device.Download(offsets_after.data());
    if ((!args.mutate_input_after_launch && score_after != score) ||
        x_after != x || offsets_after != offsets) {
        throw std::runtime_error("read-only input tensor was modified by the submission");
    }
    if (address_shift) address_shift->CheckGuards("address shift allocation");
    return 0;
}

std::vector<Arguments> ReadSequence(const Arguments &base)
{
    std::ifstream stream(base.sequence_file);
    if (!stream) {
        throw std::runtime_error("cannot open " + base.sequence_file.string());
    }
    std::vector<Arguments> cases;
    std::string line;
    for (uint32_t line_number = 1; std::getline(stream, line); ++line_number) {
        if (line.empty() || line.front() == '#') continue;
        std::istringstream fields(line);
        Arguments args = base;
        args.sequence_file.clear();
        args.benchmark_json.clear();
        args.workspace_output.clear();
        args.groups = 0;
        args.robustness_runs = 1;
        if (!(fields >> args.input_dir >> args.output_dir >> args.n >> args.d >> args.s)) {
            throw std::runtime_error("invalid sequence line " + std::to_string(line_number));
        }
        if (!(fields >> args.epsilon)) args.epsilon = base.epsilon;
        if (args.n == 0 || args.d == 0 || args.d > 256 || args.s == 0 ||
            !std::isfinite(args.epsilon) || args.epsilon < kMinimumEpsilon ||
            args.epsilon > kMaximumEpsilon) {
            throw std::runtime_error("invalid shape on sequence line " +
                std::to_string(line_number));
        }
        cases.push_back(args);
    }
    if (cases.empty()) throw std::runtime_error("sequence contains no cases");
    return cases;
}

std::string DecodeHexPath(const std::string &encoded)
{
    if (encoded == "-") return {};
    if ((encoded.size() % 2U) != 0U) {
        throw std::runtime_error("worker path has invalid hex length");
    }
    const auto nibble = [](char value) -> uint8_t {
        if (value >= '0' && value <= '9') return static_cast<uint8_t>(value - '0');
        if (value >= 'a' && value <= 'f') return static_cast<uint8_t>(value - 'a' + 10);
        if (value >= 'A' && value <= 'F') return static_cast<uint8_t>(value - 'A' + 10);
        throw std::runtime_error("worker path contains non-hex data");
    };
    std::string decoded;
    decoded.reserve(encoded.size() / 2U);
    for (size_t index = 0; index < encoded.size(); index += 2U) {
        decoded.push_back(static_cast<char>(
            (nibble(encoded[index]) << 4U) | nibble(encoded[index + 1U])));
    }
    return decoded;
}

Arguments ReadWorkerRequest(const Arguments &base, const std::string &line,
    uint64_t *request_id)
{
    std::istringstream fields(line);
    std::string operation;
    std::string input_path;
    std::string output_path;
    std::string benchmark_path;
    Arguments args = base;
    args.worker_mode = false;
    if (!(fields >> operation >> *request_id >> input_path >> output_path >> benchmark_path
          >> args.n >> args.d >> args.s >> args.epsilon >> args.warmup
          >> args.groups >> args.target_ms) || operation != "RUN") {
        throw std::runtime_error("invalid worker request");
    }
    std::string trailing;
    if (fields >> trailing) {
        throw std::runtime_error("unexpected worker request field");
    }
    args.input_dir = DecodeHexPath(input_path);
    args.output_dir = DecodeHexPath(output_path);
    args.benchmark_json = DecodeHexPath(benchmark_path);
    if (args.input_dir.empty() || args.output_dir.empty() ||
        args.n == 0 || args.d == 0 || args.s == 0 ||
        !std::isfinite(args.epsilon) || args.epsilon < kMinimumEpsilon ||
        args.epsilon > kMaximumEpsilon || args.d > 256 || args.groups > 100 ||
        (args.groups > 0 && args.benchmark_json.empty())) {
        throw std::runtime_error("invalid worker case arguments");
    }
    return args;
}

int RunWorker(const Arguments &base, Runtime &runtime)
{
    std::cout << "RSM_WORKER_READY " RSM_VARIANT "\n" << std::flush;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "QUIT") return 0;
        uint64_t request_id = 0;
        const Arguments args = ReadWorkerRequest(base, line, &request_id);
        RunCase(args, runtime, 0);
        std::cout << "RSM_WORKER_DONE " << request_id << "\n" << std::flush;
    }
    return 0;
}

int Run(int argc, char **argv)
{
    const Arguments args = ParseArguments(argc, argv);
    Runtime runtime(args.device);
    if (args.worker_mode) {
        return RunWorker(args, runtime);
    }
    if (!args.sequence_file.empty()) {
        const auto cases = ReadSequence(args);
        for (size_t index = 0; index < cases.size(); ++index) {
            RunCase(cases[index], runtime, index * 4096);
            std::cout << RSM_VARIANT << " sequence_case=" << index << " passed\n";
        }
        return 0;
    }
    for (uint32_t run = 0; run < args.robustness_runs; ++run) {
        RunCase(args, runtime, static_cast<size_t>(run) * 4096);
    }
    return 0;
}

}  // namespace RsmRunner

int main(int argc, char **argv)
{
    try {
        return RsmRunner::Run(argc, argv);
    } catch (const std::exception &error) {
        std::cerr << RSM_VARIANT << " runner: " << error.what() << '\n';
        return 2;
    }
}
