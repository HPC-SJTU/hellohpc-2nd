#include "HostRuntime.hpp"

#include "Schedule.hpp"
#include "ThreadAffinity.hpp"
#include "WorkerProcess.hpp"

#include "MaiMoeEngine.hpp"
#include "maimoe/kernel_api.hpp"
#include "maimoe/manifest.hpp"
#include "maimoe/types.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace maimoe::host {
namespace {

constexpr std::uint32_t kBucketCount = 256U;
constexpr std::size_t kFrameBytes = kOutputFrameFileBytes;
constexpr std::size_t kResultOffset = 2U * kFrameBytes;

class CreditBudget {
public:
    explicit CreditBudget(std::size_t capacity) : capacity_(capacity) {}

    bool acquire(std::size_t bytes) {
        std::unique_lock lock(mutex_);
        available_.wait(lock, [&] {
            return stopped_ || used_ == 0U || used_ + bytes <= capacity_;
        });
        if (stopped_) {
            return false;
        }
        used_ += bytes;
        return true;
    }

    bool try_acquire(std::size_t bytes) {
        std::lock_guard lock(mutex_);
        if (stopped_ || (used_ != 0U && used_ + bytes > capacity_)) {
            return false;
        }
        used_ += bytes;
        return true;
    }

    void release(std::size_t bytes) noexcept {
        {
            std::lock_guard lock(mutex_);
            if (bytes > used_) {
                std::cerr << "maimoe: invariant: CreditBudget::release(bytes=" << bytes
                          << ") > used_=" << used_ << '\n';
                std::terminate();
            }
            used_ -= bytes;
        }
        available_.notify_all();
    }

    void stop() noexcept {
        {
            std::lock_guard lock(mutex_);
            stopped_ = true;
        }
        available_.notify_all();
    }

private:
    const std::size_t capacity_;
    std::size_t used_ = 0U;
    bool stopped_ = false;
    std::mutex mutex_;
    std::condition_variable available_;
};

class CreditLease {
public:
    CreditLease() = default;
    CreditLease(CreditBudget& budget, std::size_t bytes) noexcept
        : budget_(&budget), bytes_(bytes) {}
    CreditLease(const CreditLease&) = delete;
    CreditLease& operator=(const CreditLease&) = delete;
    CreditLease(CreditLease&& other) noexcept
        : budget_(std::exchange(other.budget_, nullptr)),
          bytes_(std::exchange(other.bytes_, 0U)) {}
    CreditLease& operator=(CreditLease&& other) noexcept {
        if (this != &other) {
            reset();
            budget_ = std::exchange(other.budget_, nullptr);
            bytes_ = std::exchange(other.bytes_, 0U);
        }
        return *this;
    }
    ~CreditLease() { reset(); }

    void release(std::size_t bytes) noexcept {
        if (budget_ == nullptr || bytes > bytes_) {
            std::cerr << "maimoe: invariant: CreditLease::release(bytes=" << bytes
                      << ") budget=" << (budget_ != nullptr) << " bytes_=" << bytes_ << '\n';
            std::terminate();
        }
        budget_->release(bytes);
        bytes_ -= bytes;
        if (bytes_ == 0U) {
            budget_ = nullptr;
        }
    }

private:
    void reset() noexcept {
        if (budget_ != nullptr) {
            budget_->release(bytes_);
            budget_ = nullptr;
            bytes_ = 0U;
        }
    }

    CreditBudget* budget_ = nullptr;
    std::size_t bytes_ = 0U;
};

struct InputItem {
    std::uint64_t chart_id = 0U;
    std::string text;
    CreditLease credit;
    std::size_t schedule_index = 0U;
};

struct EncodedBuffer {
    std::array<std::uint8_t, kernel::kEncodedChartBytes> bytes;
};

class EncodedBufferPool {
public:
    class Storage {
    public:
        Storage() = default;
        Storage(const Storage&) = delete;
        Storage& operator=(const Storage&) = delete;
        Storage(Storage&& other) noexcept
            : pool_(std::exchange(other.pool_, nullptr)),
              buffer_(std::exchange(other.buffer_, nullptr)) {}
        Storage& operator=(Storage&& other) noexcept {
            if (this != &other) {
                reset();
                pool_ = std::exchange(other.pool_, nullptr);
                buffer_ = std::exchange(other.buffer_, nullptr);
            }
            return *this;
        }
        ~Storage() { reset(); }

        [[nodiscard]] std::array<std::uint8_t, kernel::kEncodedChartBytes>& bytes() noexcept {
            return buffer_->bytes;
        }
        [[nodiscard]] const std::array<std::uint8_t, kernel::kEncodedChartBytes>&
        bytes() const noexcept {
            return buffer_->bytes;
        }

    private:
        friend class EncodedBufferPool;

        Storage(EncodedBufferPool& pool, EncodedBuffer* buffer) noexcept
            : pool_(&pool), buffer_(buffer) {}

        void reset() noexcept {
            if (buffer_ != nullptr) {
                pool_->release(std::exchange(buffer_, nullptr));
                pool_ = nullptr;
            }
        }

        EncodedBufferPool* pool_ = nullptr;
        EncodedBuffer* buffer_ = nullptr;
    };

    explicit EncodedBufferPool(std::size_t maximum_buffers)
        : maximum_buffers_(maximum_buffers) {
        available_.reserve(maximum_buffers);
    }

    [[nodiscard]] Storage acquire() {
        std::unique_ptr<EncodedBuffer> buffer;
        {
            std::lock_guard lock(mutex_);
            if (!available_.empty()) {
                buffer = std::move(available_.back());
                available_.pop_back();
            }
        }
        if (buffer == nullptr) {
            buffer.reset(new EncodedBuffer);
        }
        return Storage(*this, buffer.release());
    }

private:
    void release(EncodedBuffer* buffer) noexcept {
        std::lock_guard lock(mutex_);
        if (available_.size() >= maximum_buffers_) {
            std::cerr << "maimoe: invariant: EncodedBufferPool::release pool="
                      << available_.size() << " max=" << maximum_buffers_ << '\n';
            std::terminate();
        }
        available_.emplace_back(buffer);
    }

    const std::size_t maximum_buffers_;
    std::vector<std::unique_ptr<EncodedBuffer>> available_;
    std::mutex mutex_;
};

struct EncodedItem {
    EncodedItem(std::uint64_t id, CreditLease&& lease,
                EncodedBufferPool::Storage&& encoded_storage) noexcept
        : chart_id(id), credit(std::move(lease)), storage(std::move(encoded_storage)) {}
    EncodedItem(const EncodedItem&) = delete;
    EncodedItem& operator=(const EncodedItem&) = delete;
    EncodedItem(EncodedItem&&) noexcept = default;
    EncodedItem& operator=(EncodedItem&& other) noexcept {
        if (this != &other) {
            storage = {};
            credit = {};
            chart_id = other.chart_id;
            credit = std::move(other.credit);
            storage = std::move(other.storage);
        }
        return *this;
    }

    std::uint64_t chart_id = 0U;
    CreditLease credit;
    EncodedBufferPool::Storage storage;
};

class InputQueue {
public:
    explicit InputQueue(std::size_t capacity) : capacity_(capacity) {}

    bool reserve() {
        std::unique_lock lock(mutex_);
        space_.wait(lock, [&] { return stopped_ || reserved_ < capacity_; });
        if (stopped_) {
            return false;
        }
        ++reserved_;
        return true;
    }

    void cancel_reservation() noexcept {
        {
            std::lock_guard lock(mutex_);
            if (reserved_ == 0U) {
                std::cerr << "maimoe: invariant: InputQueue::cancel_reservation reserved=0\n";
                std::terminate();
            }
            --reserved_;
        }
        space_.notify_one();
    }

    bool push_reserved(InputItem&& item) {
        std::lock_guard lock(mutex_);
        if (reserved_ == 0U) {
            std::cerr << "maimoe: invariant: InputQueue::push_reserved reserved=0\n";
            std::terminate();
        }
        if (stopped_) {
            --reserved_;
            space_.notify_one();
            return false;
        }
        queue_.push_back(std::move(item));
        data_.notify_one();
        return true;
    }

    [[nodiscard]] bool try_pop(InputItem& item) {
        std::lock_guard lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop_front();
        --reserved_;
        space_.notify_one();
        return true;
    }

    bool pop(InputItem& item) {
        std::unique_lock lock(mutex_);
        data_.wait(lock, [&] { return stopped_ || finished_ || !queue_.empty(); });
        if (queue_.empty()) {
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop_front();
        --reserved_;
        space_.notify_one();
        return true;
    }

    [[nodiscard]] bool try_pop_ordered(std::size_t expected, InputItem& item) {
        std::lock_guard lock(mutex_);
        const auto found = std::find_if(queue_.begin(), queue_.end(),
                                        [expected](const InputItem& entry) {
                                            return entry.schedule_index == expected;
                                        });
        if (found == queue_.end()) {
            return false;
        }
        item = std::move(*found);
        queue_.erase(found);
        --reserved_;
        space_.notify_one();
        return true;
    }

    bool pop_ordered(std::size_t expected, InputItem& item) {
        std::unique_lock lock(mutex_);
        data_.wait(lock, [&] {
            return stopped_ || finished_ ||
                   std::any_of(queue_.begin(), queue_.end(),
                               [expected](const InputItem& entry) {
                                   return entry.schedule_index == expected;
                               });
        });
        if (stopped_) {
            return false;
        }
        const auto found = std::find_if(queue_.begin(), queue_.end(),
                                        [expected](const InputItem& entry) {
                                            return entry.schedule_index == expected;
                                        });
        if (found == queue_.end()) {
            return false;
        }
        item = std::move(*found);
        queue_.erase(found);
        --reserved_;
        space_.notify_one();
        return true;
    }

    void finish() noexcept {
        {
            std::lock_guard lock(mutex_);
            finished_ = true;
        }
        data_.notify_all();
    }

    void stop() noexcept {
        {
            std::lock_guard lock(mutex_);
            stopped_ = true;
        }
        data_.notify_all();
        space_.notify_all();
    }

private:
    const std::size_t capacity_;
    std::size_t reserved_ = 0U;
    std::deque<InputItem> queue_;
    bool stopped_ = false;
    bool finished_ = false;
    std::mutex mutex_;
    std::condition_variable data_;
    std::condition_variable space_;
};

class OutputQueue {
public:
    OutputQueue(std::size_t partitions, PartitionSteal policy)
        : partitions_(partitions), policy_(policy) {}

    bool push(EncodedItem&& item) {
        const std::size_t partition =
            static_cast<std::size_t>(item.chart_id & 0xffU) % partitions_.size();
        Partition& target = partitions_[partition];
        {
            std::lock_guard lock(target.mutex);
            if (stopped_.load(std::memory_order_relaxed)) {
                return false;
            }
            target.queue.push_back(std::move(item));
        }
        if (policy_ == PartitionSteal::Strict) {
            target.data.notify_one();
        } else {
            {
                std::lock_guard lock(meta_mutex_);
                ++queued_;
            }
            meta_data_.notify_all();
        }
        return true;
    }

    bool pop_batch(std::size_t home, PartitionSteal policy, std::size_t maximum,
                   std::size_t& steal_cursor, std::vector<EncodedItem>& output) {
        if (policy == PartitionSteal::Strict) {
            Partition& source = partitions_[home];
            std::unique_lock lock(source.mutex);
            source.data.wait(lock, [&] {
                return stopped_.load(std::memory_order_relaxed) ||
                       finished_.load(std::memory_order_relaxed) ||
                       !source.queue.empty();
            });
            if (source.queue.empty()) {
                return false;
            }
            output.clear();
            const std::size_t count = (std::min)(maximum, source.queue.size());
            for (std::size_t index = 0U; index < count; ++index) {
                output.push_back(std::move(source.queue.front()));
                source.queue.pop_front();
            }
            steal_cursor = (home + 1U) % partitions_.size();
            return true;
        }

        for (;;) {
            if (policy == PartitionSteal::PreferLocal) {
                Partition& home_partition = partitions_[home];
                std::unique_lock lock(home_partition.mutex);
                if (!home_partition.queue.empty()) {
                    output.clear();
                    const std::size_t count =
                        (std::min)(maximum, home_partition.queue.size());
                    for (std::size_t index = 0U; index < count; ++index) {
                        output.push_back(std::move(home_partition.queue.front()));
                        home_partition.queue.pop_front();
                    }
                    lock.unlock();
                    account_popped(count);
                    return true;
                }
            }

            for (std::size_t offset = 0U; offset < partitions_.size(); ++offset) {
                const std::size_t candidate =
                    (steal_cursor + offset) % partitions_.size();
                if (policy == PartitionSteal::PreferLocal && candidate == home) {
                    continue;
                }
                std::unique_lock lock(partitions_[candidate].mutex);
                if (partitions_[candidate].queue.empty()) {
                    continue;
                }
                output.clear();
                const std::size_t count =
                    (std::min)(maximum, partitions_[candidate].queue.size());
                for (std::size_t index = 0U; index < count; ++index) {
                    output.push_back(std::move(partitions_[candidate].queue.front()));
                    partitions_[candidate].queue.pop_front();
                }
                lock.unlock();
                steal_cursor = (candidate + 1U) % partitions_.size();
                account_popped(count);
                return true;
            }

            std::unique_lock meta_lock(meta_mutex_);
            meta_data_.wait(meta_lock, [&] {
                return stopped_.load(std::memory_order_relaxed) ||
                       finished_.load(std::memory_order_relaxed) || queued_ != 0U;
            });
            if (queued_ == 0U) {
                return false;
            }
        }
    }

    void finish() noexcept {
        finished_.store(true, std::memory_order_relaxed);
        notify_all();
    }

    void stop() noexcept {
        stopped_.store(true, std::memory_order_relaxed);
        notify_all();
    }

private:
    void account_popped(std::size_t count) noexcept {
        std::lock_guard lock(meta_mutex_);
        if (count > queued_) {
            std::size_t partition_tail = 0U;
            for (const Partition& partition : partitions_) {
                partition_tail += partition.queue.size();
            }
            std::cerr << "maimoe: invariant: OutputQueue::account_popped(count=" << count
                      << ") > queued_=" << queued_
                      << " policy=" << static_cast<int>(policy_)
                      << " partitions=" << partitions_.size()
                      << " partition_tail=" << partition_tail
                      << " partition_sizes=[";
            for (const Partition& partition : partitions_) {
                std::cerr << partition.queue.size() << ',';
            }
            std::cerr << "]\n";
            std::terminate();
        }
        queued_ -= count;
    }

    void notify_all() noexcept {
        for (Partition& partition : partitions_) {
            partition.data.notify_all();
        }
        meta_data_.notify_all();
    }

    struct Partition {
        std::deque<EncodedItem> queue;
        std::mutex mutex;
        std::condition_variable data;
    };

    std::vector<Partition> partitions_;
    std::size_t queued_ = 0U;
    std::mutex meta_mutex_;
    std::condition_variable meta_data_;
    std::atomic_bool stopped_{false};
    std::atomic_bool finished_{false};
    PartitionSteal policy_;
};

[[nodiscard]] std::string bucket_name(std::uint32_t value) {
    constexpr char kHex[] = "0123456789abcdef";
    std::string output(2U, '0');
    output[0] = kHex[(value >> 4U) & 0xfU];
    output[1] = kHex[value & 0xfU];
    return output;
}

void prepare_output(const std::filesystem::path& output_dir) {
    std::error_code error;
    if (std::filesystem::exists(output_dir, error) || error) {
        throw std::runtime_error("output directory must not already exist");
    }
    if (!std::filesystem::create_directories(output_dir / "charts", error) || error) {
        throw std::runtime_error("cannot create output root");
    }
    for (std::uint32_t bucket = 0U; bucket < kBucketCount; ++bucket) {
        if (!std::filesystem::create_directory(
                output_dir / "charts" / bucket_name(bucket), error) || error) {
            throw std::runtime_error("cannot create output bucket directory");
        }
    }
}

#if defined(__linux__)

class OutputWriter {
public:
    explicit OutputWriter(const std::filesystem::path& output_dir) {
        bucket_descriptors_.fill(-1);
        const std::filesystem::path charts = output_dir / "charts";
        const int charts_descriptor =
            ::open(charts.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
        if (charts_descriptor < 0) {
            throw std::runtime_error("cannot open output charts directory");
        }
        for (std::uint32_t bucket = 0U; bucket < kBucketCount; ++bucket) {
            char name[3];
            format_bucket(name, bucket);
            bucket_descriptors_[bucket] = ::openat(
                charts_descriptor, name, O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
            if (bucket_descriptors_[bucket] < 0) {
                static_cast<void>(::close(charts_descriptor));
                close_buckets();
                throw std::runtime_error("cannot open output bucket directory");
            }
        }
        if (::close(charts_descriptor) != 0) {
            close_buckets();
            throw std::runtime_error("cannot close output charts directory");
        }
    }

    OutputWriter(const OutputWriter&) = delete;
    OutputWriter& operator=(const OutputWriter&) = delete;
    ~OutputWriter() { close_buckets(); }

    void write(const EncodedItem& item) const {
        const std::span bytes(item.storage.bytes());
        const int directory =
            bucket_descriptors_[static_cast<std::size_t>(item.chart_id & 0xffU)];
        std::array<char, 34U> filename;
        format_stem(filename.data(), item.chart_id);
        write_file(directory, filename, ".frame_begin.rgba",
                   bytes.subspan(0U, kFrameBytes));
        write_file(directory, filename, ".frame_end.rgba",
                   bytes.subspan(kFrameBytes, kFrameBytes));
        write_file(directory, filename, ".result.mmr",
                   bytes.subspan(kResultOffset, kOutputResultFileBytes));
    }

private:
    static constexpr char kHex[] = "0123456789abcdef";

    static void format_bucket(char (&output)[3], std::uint32_t value) noexcept {
        output[0] = kHex[(value >> 4U) & 0xfU];
        output[1] = kHex[value & 0xfU];
        output[2] = '\0';
    }

    static void format_stem(char* output, std::uint64_t chart_id) noexcept {
        for (std::size_t index = 0U; index < 16U; ++index) {
            output[15U - index] = kHex[chart_id & 0xfU];
            chart_id >>= 4U;
        }
    }

    static void write_file(int directory, std::array<char, 34U>& filename,
                           std::string_view suffix,
                           std::span<const std::uint8_t> bytes) {
        std::copy(suffix.begin(), suffix.end(), filename.begin() + 16U);
        filename[16U + suffix.size()] = '\0';
        const int descriptor = ::openat(
            directory, filename.data(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
        if (descriptor < 0) {
            throw std::runtime_error(std::string("cannot create output file ") +
                                     filename.data());
        }

        const std::uint8_t* cursor = bytes.data();
        std::size_t remaining = bytes.size();
        bool wrote_all = true;
        while (remaining != 0U) {
            const ssize_t count = ::write(descriptor, cursor, remaining);
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count <= 0) {
                wrote_all = false;
                break;
            }
            const std::size_t written = static_cast<std::size_t>(count);
            cursor += written;
            remaining -= written;
        }
        const bool closed = ::close(descriptor) == 0;
        if (!wrote_all || !closed) {
            throw std::runtime_error(std::string("cannot write output file ") +
                                     filename.data());
        }
    }

    void close_buckets() noexcept {
        for (int& descriptor : bucket_descriptors_) {
            if (descriptor >= 0) {
                static_cast<void>(::close(descriptor));
                descriptor = -1;
            }
        }
    }

    std::array<int, kBucketCount> bucket_descriptors_;
};

#else

class OutputWriter {
public:
    explicit OutputWriter(std::filesystem::path output_dir)
        : output_dir_(std::move(output_dir)) {}

    void write(const EncodedItem& item) const {
        const std::filesystem::path directory =
            output_dir_ / "charts" / engine::output_bucket(item.chart_id);
        const std::string stem = engine::output_stem(item.chart_id);
        const std::span bytes(item.storage.bytes());
        write_file(directory / (stem + ".frame_begin.rgba"),
                   bytes.subspan(0U, kFrameBytes));
        write_file(directory / (stem + ".frame_end.rgba"),
                   bytes.subspan(kFrameBytes, kFrameBytes));
        write_file(directory / (stem + ".result.mmr"),
                   bytes.subspan(kResultOffset, kOutputResultFileBytes));
    }

private:
    static void write_file(const std::filesystem::path& path,
                           std::span<const std::uint8_t> bytes) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("cannot create output file " + path.string());
        }
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output) {
            throw std::runtime_error("cannot write output file " + path.string());
        }
    }

    std::filesystem::path output_dir_;
};

#endif

[[nodiscard]] std::string read_chart_exact(const std::filesystem::path& path,
                                           std::uint64_t expected_bytes) {
    if (expected_bytes > static_cast<std::uint64_t>(
            (std::numeric_limits<std::size_t>::max)())) {
        throw std::runtime_error("chart is too large for this platform");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open chart file " + path.string());
    }
    std::string text(static_cast<std::size_t>(expected_bytes), '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (input.gcount() != static_cast<std::streamsize>(text.size()) ||
        input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("chart file size changed while reading " + path.string());
    }
    return text;
}

[[nodiscard]] std::chrono::milliseconds worker_timeout() {
    constexpr std::chrono::milliseconds kDefault{30000};
    std::string text;
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t size = 0U;
    if (_dupenv_s(&value, &size, "MAIMOE_WORKER_TIMEOUT_MS") == 0 && value != nullptr) {
        text = value;
    }
    std::free(value);
#else
    if (const char* const value = std::getenv("MAIMOE_WORKER_TIMEOUT_MS"); value != nullptr) {
        text = value;
    }
#endif
    if (text.empty()) {
        return kDefault;
    }
    try {
        const unsigned long milliseconds = std::stoul(text);
        if (milliseconds >= 10UL && milliseconds <= 300000UL) {
            return std::chrono::milliseconds(milliseconds);
        }
    } catch (const std::exception&) {
    }
    return kDefault;
}

[[nodiscard]] const char* partition_steal_name(PartitionSteal policy) noexcept {
    switch (policy) {
        case PartitionSteal::Strict:
            return "strict";
        case PartitionSteal::PreferLocal:
            return "prefer_local";
        case PartitionSteal::Shared:
            return "shared";
    }
    return "unknown";
}

}

int run(const std::filesystem::path& dataset_dir,
        const std::filesystem::path& output_dir,
        const std::filesystem::path& worker_executable,
        const TuneConfig& config) {
    if (!std::filesystem::is_regular_file(worker_executable)) {
        throw std::runtime_error("cannot locate trusted maimoe_kernel worker beside maimoe");
    }
    const Manifest manifest = engine::load_manifest(dataset_dir, false);
    prepare_output(output_dir);
    OutputWriter output_writer(output_dir);

    const std::uint32_t partition_count = config.io_workers == 0U
        ? 1U
        : (std::min)(config.queue_partitions, config.io_workers);
    const std::vector<std::size_t> order = make_work_order_hybrid(
        manifest, config.schedule_policy, config.schedule_secondary,
        config.schedule_hybrid_weight, partition_count);
    const std::size_t budget_bytes =
        static_cast<std::size_t>(config.pipeline_mib) * 1024U * 1024U;
    CreditBudget budget(budget_bytes);
    EncodedBufferPool encoded_buffers(manifest.charts.size());
    InputQueue input_queue((std::max<std::uint32_t>)(1U, config.prefetch_depth));
    OutputQueue output_queue(partition_count, config.partition_steal);

    std::atomic_bool failed{false};
    std::mutex error_mutex;
    std::exception_ptr first_error;
    const int core_count = allowed_core_count();
    std::vector<std::unique_ptr<WorkerProcess>> workers;
    workers.reserve(config.cpu_workers);
    try {
        for (std::uint32_t index = 0U; index < config.cpu_workers; ++index) {
            workers.push_back(std::make_unique<WorkerProcess>(
                worker_executable, worker_timeout(),
                static_cast<int>(index % static_cast<std::uint32_t>(core_count))));
        }
    } catch (...) {
        for (const std::unique_ptr<WorkerProcess>& worker : workers) {
            worker->abort();
        }
        throw;
    }

    const auto fail = [&](std::exception_ptr error) noexcept {
        bool expected = false;
        if (failed.compare_exchange_strong(expected, true)) {
            {
                std::lock_guard lock(error_mutex);
                first_error = std::move(error);
            }
            budget.stop();
            input_queue.stop();
            output_queue.stop();
            for (const std::unique_ptr<WorkerProcess>& worker : workers) {
                worker->abort();
            }
        }
    };

    const auto chart_reservation = [](const ManifestChart& chart) {
        const std::size_t input_bytes = static_cast<std::size_t>(chart.bytes);
        return input_bytes + kernel::kEncodedChartBytes;
    };
    const auto read_reserved_item = [&](const ManifestChart& chart) -> InputItem {
        const std::size_t reservation = chart_reservation(chart);
        CreditLease credit(budget, reservation);
        std::string text = read_chart_exact(dataset_dir / chart.path, chart.bytes);
        return InputItem{chart.id, std::move(text), std::move(credit)};
    };
    const auto read_item = [&](const ManifestChart& chart) -> InputItem {
        const std::size_t reservation = chart_reservation(chart);
        if (!budget.acquire(reservation)) {
            throw std::runtime_error("pipeline stopped while reserving input credit");
        }
        return read_reserved_item(chart);
    };

    const auto process_item = [&](WorkerProcess& worker, InputItem&& input) -> EncodedItem {
        const std::size_t input_bytes = input.text.size();
        EncodedBufferPool::Storage storage = encoded_buffers.acquire();
        worker.process(input.chart_id, input.text, storage.bytes());
        std::string{}.swap(input.text);
        input.credit.release(input_bytes);
        return EncodedItem{input.chart_id, std::move(input.credit), std::move(storage)};
    };

    std::vector<std::jthread> io_threads;
    std::vector<std::jthread> reader_threads;
    std::vector<std::jthread> compute_threads;
    std::atomic_size_t next{0U};
    std::atomic_size_t expected_index{0U};
    std::atomic_uint32_t remaining_readers{0U};

    try {
        if (config.io_workers > 0U) {
            io_threads.reserve(config.io_workers);
            for (std::uint32_t index = 0U; index < config.io_workers; ++index) {
                const int io_core = static_cast<int>(
                    (config.cpu_workers + index / 2U) %
                    static_cast<std::uint32_t>(core_count));
                io_threads.emplace_back([&, partition = index % partition_count, io_core] {
                    pin_current(io_core);
                    try {
                        std::size_t steal_cursor = (partition + 1U) % partition_count;
                        std::vector<EncodedItem> batch;
                        batch.reserve(config.write_batch);
                        while (output_queue.pop_batch(partition, config.partition_steal,
                                                      config.write_batch, steal_cursor, batch)) {
                            for (const EncodedItem& item : batch) {
                                output_writer.write(item);
                            }
                            batch.clear();
                        }
                    } catch (...) {
                        fail(std::current_exception());
                    }
                });
            }
        }

        if (config.input_readers > 0U) {
            const std::uint32_t reader_count = config.input_readers;
            remaining_readers.store(reader_count, std::memory_order_relaxed);
            reader_threads.reserve(reader_count);
            for (std::uint32_t reader = 0U; reader < reader_count; ++reader) {
                const int reader_core = static_cast<int>(
                    (config.cpu_workers + (config.io_workers + 1U) / 2U +
                     reader / 4U) %
                    static_cast<std::uint32_t>(core_count));
                reader_threads.emplace_back([&, reader_core] {
                    pin_current(reader_core);
                    try {
                        while (!failed.load(std::memory_order_relaxed)) {
                            const std::size_t position =
                                next.fetch_add(1U, std::memory_order_relaxed);
                            if (position >= order.size()) {
                                break;
                            }
                            const ManifestChart& chart = manifest.charts[order[position]];
                            if (!input_queue.reserve()) {
                                break;
                            }
                            try {
                                InputItem item = read_item(chart);
                                item.schedule_index = position;
                                if (!input_queue.push_reserved(std::move(item))) {
                                    break;
                                }
                            } catch (...) {
                                input_queue.cancel_reservation();
                                throw;
                            }
                        }
                    } catch (...) {
                        fail(std::current_exception());
                    }
                    if (remaining_readers.fetch_sub(1U) == 1U) {
                        input_queue.finish();
                    }
                });
            }
        }

        compute_threads.reserve(config.cpu_workers);
        for (std::uint32_t index = 0U; index < config.cpu_workers; ++index) {
            const int compute_core =
                static_cast<int>(index % static_cast<std::uint32_t>(core_count));
            compute_threads.emplace_back([&, index, compute_core] {
                pin_current(compute_core);
                std::vector<EncodedItem> inline_batch;
                inline_batch.reserve(config.write_batch);
                const auto flush_inline = [&] {
                    for (const EncodedItem& item : inline_batch) {
                        output_writer.write(item);
                    }
                    inline_batch.clear();
                };
                const auto submit = [&](EncodedItem&& item) {
                    if (config.io_workers > 0U) {
                        if (!output_queue.push(std::move(item))) {
                            throw std::runtime_error("output queue stopped");
                        }
                    } else {
                        inline_batch.push_back(std::move(item));
                        if (inline_batch.size() >= config.write_batch) {
                            flush_inline();
                        }
                    }
                };
                try {
                    WorkerProcess& worker = *workers[index];
                    if (config.input_readers == 0U) {
                        while (!failed.load(std::memory_order_relaxed)) {
                            const std::size_t begin =
                                next.fetch_add(config.compute_batch, std::memory_order_relaxed);
                            if (begin >= order.size()) {
                                break;
                            }
                            const std::size_t end =
                                (std::min)(order.size(), begin + config.compute_batch);
                            for (std::size_t position = begin; position < end; ++position) {
                                const ManifestChart& chart = manifest.charts[order[position]];
                                InputItem input;
                                if (config.io_workers == 0U) {
                                    const std::size_t reservation = chart_reservation(chart);
                                    if (!budget.try_acquire(reservation)) {
                                        flush_inline();
                                        if (!budget.acquire(reservation)) {
                                            throw std::runtime_error(
                                                "pipeline stopped while reserving input credit");
                                        }
                                    }
                                    input = read_reserved_item(chart);
                                } else {
                                    input = read_item(chart);
                                }
                                submit(process_item(worker, std::move(input)));
                            }
                            if (config.io_workers == 0U) {
                                flush_inline();
                            }
                        }
                    } else {
                        InputItem item;
                        while (!failed.load(std::memory_order_relaxed)) {
                            const std::size_t expected =
                                expected_index.fetch_add(1U, std::memory_order_relaxed);
                            if (!input_queue.try_pop_ordered(expected, item)) {
                                if (config.io_workers == 0U) {
                                    flush_inline();
                                }
                                if (!input_queue.pop_ordered(expected, item)) {
                                    break;
                                }
                            }
                            submit(process_item(worker, std::move(item)));
                            for (std::uint32_t extra = 1U; extra < config.compute_batch;
                                 ++extra) {
                                const std::size_t next_expected = expected_index.fetch_add(
                                    1U, std::memory_order_relaxed);
                                if (!input_queue.try_pop_ordered(next_expected, item) &&
                                    !input_queue.pop_ordered(next_expected, item)) {
                                    break;
                                }
                                submit(process_item(worker, std::move(item)));
                            }
                        }
                    }
                    if (config.io_workers == 0U) {
                        flush_inline();
                    }
                } catch (...) {
                    fail(std::current_exception());
                }
            });
        }

        for (std::jthread& thread : compute_threads) {
            thread.join();
        }
        output_queue.finish();
        input_queue.stop();
        budget.stop();
        for (std::jthread& thread : reader_threads) {
            thread.join();
        }
        for (std::jthread& thread : io_threads) {
            thread.join();
        }
    } catch (...) {
        fail(std::current_exception());
        throw;
    }

    if (first_error != nullptr) {
        std::rethrow_exception(first_error);
    }
    std::cerr << "maimoe charts=" << manifest.chart_count
              << " output_files=" << manifest.output_file_count
              << " output_bytes=" << manifest.output_logical_bytes
              << " cpu_workers=" << config.cpu_workers
              << " input_readers=" << config.input_readers
              << " prefetch_depth=" << config.prefetch_depth
              << " compute_batch=" << config.compute_batch
              << " pipeline_mib=" << config.pipeline_mib
              << " io_workers=" << config.io_workers
              << " write_batch=" << config.write_batch
              << " queue_partitions=" << partition_count
              << " partition_steal=" << partition_steal_name(config.partition_steal)
              << " schedule_policy=" << schedule_policy_name(config.schedule_policy)
              << " schedule_secondary=" << schedule_policy_name(config.schedule_secondary)
              << " schedule_hybrid_weight=" << config.schedule_hybrid_weight
              << " allowed_cores=" << core_count
              << " kernel_worker=process"
              << '\n';
    return 0;
}

}
