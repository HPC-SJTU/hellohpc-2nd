// Process boundary between the trusted evaluator and an untrusted kernel.
//
// The trusted harness never links the submission.  A worker is started by
// fork+exec of a minimal executable, so it cannot inherit parent memory
// containing inputs or references.  The worker installs seccomp before
// dlopen(); only after constructors have returned does the parent transfer the
// exact memfds over a private SOCK_SEQPACKET socket.  It stops at READY/DONE
// boundaries; only
// the parent generates inputs, validates output, measures time and prints.
//
// This is defence in depth.  The formal runner must also enter its fail-closed
// user/mount/PID/network namespace before launching this binary: stage-one
// dlopen necessarily permits read-only file opens for the ELF loader.
#pragma once

#if !defined(__linux__)
#error "isolated_runner requires Linux"
#endif

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/memfd.h>
#include <linux/sched.h>
#include <linux/seccomp.h>
#include <limits.h>
#include <omp.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace isolated {

struct Region {
  void* address = nullptr;
  std::size_t bytes = 0;
  int fd = -1;
  bool readonly_in_worker = false;
};

template <typename T>
class SharedArray {
 public:
  explicit SharedArray(std::size_t count) : count_(count) {
    if (!count || count > SIZE_MAX / sizeof(T)) {
      throw std::invalid_argument("invalid shared-array size");
    }
    bytes_ = count * sizeof(T);
    fd_ = static_cast<int>(syscall(SYS_memfd_create, "cheatsheet-kernel-buffer",
                                   MFD_ALLOW_SEALING | MFD_CLOEXEC));
    if (fd_ < 0 || ftruncate(fd_, static_cast<off_t>(bytes_)) != 0) {
      if (fd_ >= 0) close(fd_);
      throw std::runtime_error(std::string("create kernel memfd: ") +
                               std::strerror(errno));
    }
    data_ = static_cast<T*>(mmap(nullptr, bytes_, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, fd_, 0));
    if (data_ == MAP_FAILED) {
      data_ = nullptr;
      close(fd_);
      fd_ = -1;
      throw std::runtime_error(std::string("map kernel memfd: ") +
                               std::strerror(errno));
    }
    // The worker can change contents only through its mapping; it cannot resize
    // the backing object and corrupt mapping metadata.
    (void)fcntl(fd_, F_ADD_SEALS, F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL);
    char descriptor_path[64];
    std::snprintf(descriptor_path, sizeof(descriptor_path), "/proc/self/fd/%d", fd_);
    readonly_fd_ = open(descriptor_path, O_RDONLY | O_CLOEXEC);
    if (readonly_fd_ < 0) {
      munmap(data_, bytes_);
      data_ = nullptr;
      close(fd_);
      fd_ = -1;
      throw std::runtime_error(std::string("open read-only kernel memfd: ") +
                               std::strerror(errno));
    }
  }

  ~SharedArray() {
    if (data_) munmap(data_, bytes_);
    if (readonly_fd_ >= 0) close(readonly_fd_);
    if (fd_ >= 0) close(fd_);
  }
  SharedArray(const SharedArray&) = delete;
  SharedArray& operator=(const SharedArray&) = delete;

  T* data() { return data_; }
  const T* data() const { return data_; }
  std::size_t size() const { return count_; }
  std::size_t bytes() const { return bytes_; }
  Region input_region() const { return Region{data_, bytes_, readonly_fd_, true}; }
  Region output_region() const { return Region{data_, bytes_, fd_, false}; }

 private:
  T* data_ = nullptr;
  std::size_t count_ = 0;
  std::size_t bytes_ = 0;
  int fd_ = -1;
  int readonly_fd_ = -1;
};

struct Invocation {
  bool ok = false;
  double elapsed_ms = 0.0;
  std::string error;
};

inline double monotonic_seconds() {
  struct timespec ts {};
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    throw std::runtime_error("clock_gettime failed");
  }
  return static_cast<double>(ts.tv_sec) +
         static_cast<double>(ts.tv_nsec) * 1e-9;
}

// Return kernel-supplied entropy for one trusted input generation.  Do not
// replace this with std::random_device or a PRNG seeded from the public CLI:
// predictable future inputs let a submitted kernel precompute later benchmark
// answers during an earlier timed invocation.  Only the trusted parent calls
// this helper, and the returned value is never sent in worker argv/control data.
inline std::uint64_t trusted_random_u64() {
  std::uint64_t value = 0;
  unsigned char* output = reinterpret_cast<unsigned char*>(&value);
  std::size_t done = 0;
  while (done < sizeof(value)) {
    const long amount = syscall(SYS_getrandom, output + done,
                                sizeof(value) - done, 0);
    if (amount < 0 && errno == EINTR) continue;
    if (amount <= 0) {
      throw std::runtime_error(std::string("getrandom for trusted input: ") +
                               (amount < 0 ? std::strerror(errno) :
                                             "unexpected end of entropy"));
    }
    done += static_cast<std::size_t>(amount);
  }
  return value;
}

namespace trusted_seed_detail {

inline std::uint64_t rotate_left(std::uint64_t value, int bits) {
  return (value << bits) | (value >> (64 - bits));
}

inline std::uint64_t load_le64(const unsigned char* input) {
  std::uint64_t value = 0;
  for (int index = 0; index < 8; ++index) {
    value |= static_cast<std::uint64_t>(input[index]) << (8 * index);
  }
  return value;
}

inline void store_le64(unsigned char* output, std::uint64_t value) {
  for (int index = 0; index < 8; ++index) {
    output[index] = static_cast<unsigned char>(value >> (8 * index));
  }
}

inline void sip_round(std::uint64_t& v0, std::uint64_t& v1,
                      std::uint64_t& v2, std::uint64_t& v3) {
  v0 += v1;
  v1 = rotate_left(v1, 13);
  v1 ^= v0;
  v0 = rotate_left(v0, 32);
  v2 += v3;
  v3 = rotate_left(v3, 16);
  v3 ^= v2;
  v0 += v3;
  v3 = rotate_left(v3, 21);
  v3 ^= v0;
  v2 += v1;
  v1 = rotate_left(v1, 17);
  v1 ^= v2;
  v2 = rotate_left(v2, 32);
}

// SipHash-2-4 as specified by Aumasson and Bernstein.  The small generic
// implementation also makes it possible to verify the published test vector.
inline std::uint64_t siphash24(const unsigned char* message, std::size_t length,
                               std::uint64_t k0, std::uint64_t k1) {
  std::uint64_t v0 = k0 ^ UINT64_C(0x736f6d6570736575);
  std::uint64_t v1 = k1 ^ UINT64_C(0x646f72616e646f6d);
  std::uint64_t v2 = k0 ^ UINT64_C(0x6c7967656e657261);
  std::uint64_t v3 = k1 ^ UINT64_C(0x7465646279746573);
  const std::size_t full_bytes = length & ~static_cast<std::size_t>(7);
  for (std::size_t offset = 0; offset < full_bytes; offset += 8) {
    const std::uint64_t word = load_le64(message + offset);
    v3 ^= word;
    sip_round(v0, v1, v2, v3);
    sip_round(v0, v1, v2, v3);
    v0 ^= word;
  }
  std::uint64_t final = static_cast<std::uint64_t>(length) << 56;
  for (std::size_t offset = full_bytes; offset < length; ++offset) {
    final |= static_cast<std::uint64_t>(message[offset])
             << (8 * (offset - full_bytes));
  }
  v3 ^= final;
  sip_round(v0, v1, v2, v3);
  sip_round(v0, v1, v2, v3);
  v0 ^= final;
  v2 ^= UINT64_C(0xff);
  for (int round = 0; round < 4; ++round) sip_round(v0, v1, v2, v3);
  return v0 ^ v1 ^ v2 ^ v3;
}

inline int hex_nibble(unsigned char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return -1;
}

struct PairedInputState {
  bool enabled = false;
  std::uint64_t k0 = 0;
  std::uint64_t k1 = 0;
  std::uint64_t ordinal = 0;
};

inline PairedInputState read_paired_input_state() {
  const char* marker = std::getenv("CHEATSHEET_PAIRED_INPUTS");
  if (!marker) return {};
  if (std::strcmp(marker, "stdin") != 0) {
    throw std::runtime_error("invalid paired-input transport marker");
  }

  // Read through EOF so extra bytes are rejected too.  The submitted worker
  // closes every fd except its private control socket before exec, so it never
  // inherits this parent-only pipe or the key carried by it.
  unsigned char encoded[34] = {};
  std::size_t used = 0;
  while (true) {
    unsigned char chunk[64];
    const ssize_t amount = read(STDIN_FILENO, chunk, sizeof(chunk));
    if (amount < 0 && errno == EINTR) continue;
    if (amount < 0) {
      close(STDIN_FILENO);
      throw std::runtime_error("cannot read paired-input domain");
    }
    if (amount == 0) break;
    if (used + static_cast<std::size_t>(amount) > sizeof(encoded)) {
      close(STDIN_FILENO);
      throw std::runtime_error("invalid paired-input domain encoding");
    }
    std::memcpy(encoded + used, chunk, static_cast<std::size_t>(amount));
    used += static_cast<std::size_t>(amount);
  }
  close(STDIN_FILENO);
  if (used != 33 || encoded[32] != '\n') {
    throw std::runtime_error("invalid paired-input domain encoding");
  }
  unsigned char key[16] = {};
  for (std::size_t index = 0; index < 16; ++index) {
    const int high = hex_nibble(encoded[index * 2]);
    const int low = hex_nibble(encoded[index * 2 + 1]);
    if (high < 0 || low < 0) {
      std::memset(encoded, 0, sizeof(encoded));
      throw std::runtime_error("invalid paired-input domain encoding");
    }
    key[index] = static_cast<unsigned char>((high << 4) | low);
  }
  PairedInputState state;
  state.enabled = true;
  state.k0 = load_le64(key);
  state.k1 = load_le64(key + 8);
  std::memset(encoded, 0, sizeof(encoded));
  std::memset(key, 0, sizeof(key));
  return state;
}

inline PairedInputState& paired_input_state() {
  static PairedInputState state = read_paired_input_state();
  return state;
}

}  // namespace trusted_seed_detail

// With no formal paired-input marker, preserve fresh getrandom entropy on
// every call.  Formal paired runs instead deterministically derive each input
// from a secret 128-bit stdin key plus the public seed, logical domain and
// per-process call ordinal.  The key itself never enters argv or environment.
inline std::uint64_t trusted_input_seed(std::uint64_t public_seed,
                                        std::uint64_t domain) {
  trusted_seed_detail::PairedInputState& state =
      trusted_seed_detail::paired_input_state();
  if (state.enabled) {
    if (state.ordinal == UINT64_MAX) {
      throw std::runtime_error("paired-input call ordinal exhausted");
    }
    unsigned char message[24];
    trusted_seed_detail::store_le64(message, public_seed);
    trusted_seed_detail::store_le64(message + 8, domain);
    trusted_seed_detail::store_le64(message + 16, state.ordinal++);
    return trusted_seed_detail::siphash24(
        message, sizeof(message), state.k0, state.k1);
  }
  std::uint64_t value = trusted_random_u64() ^ public_seed ^
                        (domain + UINT64_C(0x9e3779b97f4a7c15));
  value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
  value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
  return value ^ (value >> 31);
}

namespace detail {

#if defined(__aarch64__)
constexpr std::uint32_t kAuditArch = AUDIT_ARCH_AARCH64;
#elif defined(__x86_64__)
constexpr std::uint32_t kAuditArch = AUDIT_ARCH_X86_64;
#else
#error "unsupported architecture"
#endif

inline void lower_limit(int resource, rlim_t requested) {
  struct rlimit current {};
  if (getrlimit(resource, &current) != 0) _exit(120);
  const rlim_t hard = std::min(current.rlim_max, requested);
  const rlim_t soft = std::min(current.rlim_cur, hard);
  struct rlimit limit {soft, hard};
  if (setrlimit(resource, &limit) != 0) _exit(120);
}

inline void apply_limits() {
  lower_limit(RLIMIT_CPU, 600);
#if !defined(__SANITIZE_ADDRESS__)
  lower_limit(RLIMIT_AS, static_cast<rlim_t>(4) * 1024 * 1024 * 1024);
#endif
}

inline bool retained(int fd, const std::vector<int>& retained_fds) {
  return std::find(retained_fds.begin(), retained_fds.end(), fd) != retained_fds.end();
}

inline void close_except(const std::vector<int>& retained_fds) {
  struct rlimit limit {};
  unsigned long maximum = 65536;
  if (getrlimit(RLIMIT_NOFILE, &limit) == 0) {
    maximum = static_cast<unsigned long>(
        std::min<rlim_t>(limit.rlim_cur, static_cast<rlim_t>(65536)));
  }
  for (unsigned long raw = 0; raw < maximum; ++raw) {
    const int fd = static_cast<int>(raw);
    if (!retained(fd, retained_fds)) close(fd);
  }
}

inline bool checked_range(std::uint64_t offset, std::uint64_t length,
                          std::size_t size) {
  return offset <= size && length <= size - static_cast<std::size_t>(offset);
}

template <typename T>
inline const T* object_at(const std::vector<unsigned char>& image,
                          std::uint64_t offset, std::size_t count = 1) {
  if (count > SIZE_MAX / sizeof(T) ||
      !checked_range(offset, count * sizeof(T), image.size())) return nullptr;
  return reinterpret_cast<const T*>(image.data() + offset);
}

inline bool attest_shared_object_fd(int fd, const char* expected_symbol) {
  if (!expected_symbol || !expected_symbol[0] ||
      std::strchr(expected_symbol, '/')) return false;
  struct stat metadata {};
  if (fstat(fd, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
      metadata.st_nlink == 0 || metadata.st_size < (off_t)sizeof(Elf64_Ehdr) ||
      metadata.st_size > (off_t)(256ULL * 1024 * 1024)) return false;
  std::vector<unsigned char> image(static_cast<std::size_t>(metadata.st_size));
  std::size_t done = 0;
  while (done < image.size()) {
    const ssize_t amount = pread(fd, image.data() + done, image.size() - done,
                                 static_cast<off_t>(done));
    if (amount <= 0) return false;
    done += static_cast<std::size_t>(amount);
  }
  const Elf64_Ehdr* header = object_at<Elf64_Ehdr>(image, 0);
  if (!header || std::memcmp(header->e_ident, ELFMAG, SELFMAG) != 0 ||
      header->e_ident[EI_CLASS] != ELFCLASS64 ||
      header->e_ident[EI_DATA] != ELFDATA2LSB || header->e_type != ET_DYN ||
      header->e_ehsize != sizeof(Elf64_Ehdr) ||
      header->e_phentsize != sizeof(Elf64_Phdr) ||
      header->e_shentsize != sizeof(Elf64_Shdr)) return false;
#if defined(__aarch64__)
  if (header->e_machine != EM_AARCH64) return false;
#elif defined(__x86_64__)
  if (header->e_machine != EM_X86_64) return false;
#endif
  const Elf64_Phdr* programs =
      object_at<Elf64_Phdr>(image, header->e_phoff, header->e_phnum);
  if (!programs || !header->e_phnum) return false;
  const Elf64_Phdr* dynamic_program = nullptr;
  std::vector<const Elf64_Phdr*> loads;
  for (std::size_t i = 0; i < header->e_phnum; ++i) {
    if (programs[i].p_type == PT_DYNAMIC) {
      if (dynamic_program) return false;
      dynamic_program = &programs[i];
    }
    if (programs[i].p_type == PT_LOAD) {
      const Elf64_Phdr& load = programs[i];
      if (!load.p_memsz || load.p_filesz > load.p_memsz ||
          !checked_range(load.p_offset, load.p_filesz, image.size()) ||
          load.p_vaddr > UINT64_MAX - load.p_memsz ||
          (load.p_align > 1 &&
           ((load.p_align & (load.p_align - 1)) != 0 ||
            load.p_vaddr % load.p_align != load.p_offset % load.p_align))) {
        return false;
      }
      loads.push_back(&load);
    }
  }
  if (!dynamic_program || loads.empty()) return false;
  // A virtual address must have exactly one file interpretation.  Linux maps
  // PT_LOAD segments, not sections, so reject overlapping load ranges before
  // translating any loader-owned DT_* pointer.
  for (std::size_t i = 0; i < loads.size(); ++i) {
    for (std::size_t j = i + 1; j < loads.size(); ++j) {
      const std::uint64_t first_begin = loads[i]->p_vaddr;
      const std::uint64_t first_end = first_begin + loads[i]->p_memsz;
      const std::uint64_t second_begin = loads[j]->p_vaddr;
      const std::uint64_t second_end = second_begin + loads[j]->p_memsz;
      if (first_begin < second_end && second_begin < first_end) return false;
    }
  }
  auto virtual_to_offset = [&](std::uint64_t address,
                               std::uint64_t length) -> std::uint64_t {
    if (address > UINT64_MAX - length) return UINT64_MAX;
    std::uint64_t result = UINT64_MAX;
    std::size_t matches = 0;
    for (const Elf64_Phdr* candidate : loads) {
      const Elf64_Phdr& segment = *candidate;
      if (address < segment.p_vaddr) continue;
      const std::uint64_t delta = address - segment.p_vaddr;
      if (delta <= segment.p_filesz && length <= segment.p_filesz - delta &&
          checked_range(segment.p_offset + delta, length, image.size())) {
        result = segment.p_offset + delta;
        ++matches;
      }
    }
    return matches == 1 ? result : UINT64_MAX;
  };

  if (!dynamic_program->p_filesz ||
      dynamic_program->p_filesz % sizeof(Elf64_Dyn) ||
      virtual_to_offset(dynamic_program->p_vaddr, dynamic_program->p_filesz) !=
          dynamic_program->p_offset) return false;
  const std::size_t dynamic_count =
      dynamic_program->p_filesz / sizeof(Elf64_Dyn);
  const Elf64_Dyn* dynamic =
      object_at<Elf64_Dyn>(image, dynamic_program->p_offset, dynamic_count);
  if (!dynamic) return false;

  struct DynamicValue {
    bool seen = false;
    std::uint64_t value = 0;
  };
  auto set_once = [](DynamicValue& destination, std::uint64_t value) {
    if (destination.seen) return false;
    destination.seen = true;
    destination.value = value;
    return true;
  };
  DynamicValue string_address, string_size, symbol_address, symbol_entry;
  DynamicValue hash_address, gnu_hash_address;
  std::vector<std::uint64_t> needed;
  struct Relocations {
    DynamicValue address, bytes, entry;
  };
  Relocations rela, rel;
  DynamicValue jump_address, jump_bytes, jump_type;
  bool flags_seen = false;
  bool terminated = false;
  for (std::size_t i = 0; i < dynamic_count; ++i) {
    const Elf64_Dyn& entry = dynamic[i];
    if (entry.d_tag == DT_NULL) { terminated = true; break; }
    switch (entry.d_tag) {
      case DT_INIT: case DT_FINI: case DT_INIT_ARRAY: case DT_FINI_ARRAY:
#ifdef DT_PREINIT_ARRAY
      case DT_PREINIT_ARRAY:
#endif
      case DT_RPATH: case DT_RUNPATH: case DT_TEXTREL:
#ifdef DT_AUDIT
      case DT_AUDIT:
#endif
#ifdef DT_DEPAUDIT
      case DT_DEPAUDIT:
#endif
#ifdef DT_FILTER
      case DT_FILTER:
#endif
#ifdef DT_AUXILIARY
      case DT_AUXILIARY:
#endif
        return false;
      case DT_STRTAB:
        if (!set_once(string_address, entry.d_un.d_ptr)) return false;
        break;
      case DT_STRSZ:
        if (!set_once(string_size, entry.d_un.d_val)) return false;
        break;
      case DT_SYMTAB:
        if (!set_once(symbol_address, entry.d_un.d_ptr)) return false;
        break;
      case DT_SYMENT:
        if (!set_once(symbol_entry, entry.d_un.d_val)) return false;
        break;
      case DT_HASH:
        if (!set_once(hash_address, entry.d_un.d_ptr)) return false;
        break;
#ifdef DT_GNU_HASH
      case DT_GNU_HASH:
        if (!set_once(gnu_hash_address, entry.d_un.d_ptr)) return false;
        break;
#endif
      case DT_NEEDED: needed.push_back(entry.d_un.d_val); break;
      case DT_RELA:
        if (!set_once(rela.address, entry.d_un.d_ptr)) return false;
        break;
      case DT_RELASZ:
        if (!set_once(rela.bytes, entry.d_un.d_val)) return false;
        break;
      case DT_RELAENT:
        if (!set_once(rela.entry, entry.d_un.d_val)) return false;
        break;
      case DT_REL:
        if (!set_once(rel.address, entry.d_un.d_ptr)) return false;
        break;
      case DT_RELSZ:
        if (!set_once(rel.bytes, entry.d_un.d_val)) return false;
        break;
      case DT_RELENT:
        if (!set_once(rel.entry, entry.d_un.d_val)) return false;
        break;
      case DT_JMPREL:
        if (!set_once(jump_address, entry.d_un.d_ptr)) return false;
        break;
      case DT_PLTRELSZ:
        if (!set_once(jump_bytes, entry.d_un.d_val)) return false;
        break;
      case DT_PLTREL:
        if (!set_once(jump_type, entry.d_un.d_val)) return false;
        break;
      case DT_FLAGS:
        if (flags_seen) return false;
        flags_seen = true;
        if (entry.d_un.d_val & DF_TEXTREL) return false;
        break;
      default: break;
    }
  }
  if (!terminated || !string_address.seen || !string_address.value ||
      !string_size.seen || !string_size.value || !symbol_address.seen ||
      !symbol_address.value || !symbol_entry.seen ||
      symbol_entry.value != sizeof(Elf64_Sym)) return false;
  const std::uint64_t string_offset =
      virtual_to_offset(string_address.value, string_size.value);
  if (string_offset == UINT64_MAX) return false;
  const char* strings = reinterpret_cast<const char*>(image.data() + string_offset);
  const char* allowed[] = {"libgomp.so.1", "libstdc++.so.6", "libm.so.6",
                           "libgcc_s.so.1", "libc.so.6"};
  for (std::uint64_t index : needed) {
    if (index >= string_size.value ||
        !std::memchr(strings + index, '\0', string_size.value - index))
      return false;
    bool okay = false;
    for (const char* name : allowed) okay |= std::strcmp(strings + index, name) == 0;
    if (!okay) return false;
    void* preloaded = dlopen(strings + index, RTLD_NOW | RTLD_NOLOAD | RTLD_LOCAL);
    if (!preloaded) return false;
    dlclose(preloaded);
  }
  const Elf64_Shdr* sections =
      object_at<Elf64_Shdr>(image, header->e_shoff, header->e_shnum);
  if (!sections || !header->e_shnum) return false;
  // There is exactly one canonical dynamic symbol section, and it must describe
  // the same bytes the loader reaches via DT_SYMTAB.  Merely scanning every
  // SHT_DYNSYM is unsafe: section headers are ignored by dlopen and can point at
  // a harmless fake copy while DT_SYMTAB points at executable IFUNC entries.
  const Elf64_Shdr* dynamic_symbols = nullptr;
  std::size_t dynamic_symbol_sections = 0;
  for (std::size_t i = 0; i < header->e_shnum; ++i) {
    if (sections[i].sh_type == SHT_DYNSYM) {
      ++dynamic_symbol_sections;
      dynamic_symbols = &sections[i];
    }
    if (sections[i].sh_type == SHT_RELA || sections[i].sh_type == SHT_REL) {
      const bool with_addend = sections[i].sh_type == SHT_RELA;
      const std::size_t expected = with_addend ? sizeof(Elf64_Rela) : sizeof(Elf64_Rel);
      if (sections[i].sh_entsize != expected || sections[i].sh_size % expected)
        return false;
      const std::size_t count = sections[i].sh_size / expected;
      if (with_addend) {
        const Elf64_Rela* entries =
            object_at<Elf64_Rela>(image, sections[i].sh_offset, count);
        if (!entries) return false;
        for (std::size_t j = 0; j < count; ++j) {
#if defined(__aarch64__)
          if (ELF64_R_TYPE(entries[j].r_info) == R_AARCH64_IRELATIVE) return false;
#elif defined(__x86_64__)
          if (ELF64_R_TYPE(entries[j].r_info) == R_X86_64_IRELATIVE) return false;
#endif
        }
      } else {
        const Elf64_Rel* entries =
            object_at<Elf64_Rel>(image, sections[i].sh_offset, count);
        if (!entries) return false;
        for (std::size_t j = 0; j < count; ++j) {
#if defined(__aarch64__)
          if (ELF64_R_TYPE(entries[j].r_info) == R_AARCH64_IRELATIVE) return false;
#elif defined(__x86_64__)
          if (ELF64_R_TYPE(entries[j].r_info) == R_X86_64_IRELATIVE) return false;
#endif
        }
      }
    }
  }
  if (dynamic_symbol_sections != 1 || !dynamic_symbols ||
      !(dynamic_symbols->sh_flags & SHF_ALLOC) ||
      dynamic_symbols->sh_entsize != sizeof(Elf64_Sym) ||
      !dynamic_symbols->sh_size ||
      dynamic_symbols->sh_size % sizeof(Elf64_Sym) ||
      dynamic_symbols->sh_link >= header->e_shnum) return false;
  const std::uint64_t symbol_offset = virtual_to_offset(
      symbol_address.value, dynamic_symbols->sh_size);
  if (symbol_offset == UINT64_MAX ||
      dynamic_symbols->sh_addr != symbol_address.value ||
      dynamic_symbols->sh_offset != symbol_offset) return false;
  const Elf64_Shdr& dynamic_strings = sections[dynamic_symbols->sh_link];
  if (dynamic_strings.sh_type != SHT_STRTAB ||
      !(dynamic_strings.sh_flags & SHF_ALLOC) ||
      dynamic_strings.sh_addr != string_address.value ||
      dynamic_strings.sh_offset != string_offset ||
      dynamic_strings.sh_size != string_size.value) return false;
  std::size_t matching_string_sections = 0;
  for (std::size_t i = 0; i < header->e_shnum; ++i) {
    if (sections[i].sh_type == SHT_STRTAB &&
        sections[i].sh_addr == string_address.value &&
        sections[i].sh_offset == string_offset &&
        sections[i].sh_size == string_size.value) {
      ++matching_string_sections;
    }
  }
  if (matching_string_sections != 1) return false;

  const std::size_t symbol_count =
      dynamic_symbols->sh_size / sizeof(Elf64_Sym);
  if (!symbol_count || dynamic_symbols->sh_info > symbol_count) return false;
  const Elf64_Sym* symbols =
      object_at<Elf64_Sym>(image, symbol_offset, symbol_count);
  if (!symbols) return false;
  std::size_t expected_definitions = 0;
  for (std::size_t i = 0; i < symbol_count; ++i) {
    const Elf64_Sym& symbol = symbols[i];
    if (symbol.st_name >= string_size.value ||
        !std::memchr(strings + symbol.st_name, '\0',
                     string_size.value - symbol.st_name)) return false;
    const unsigned type = ELF64_ST_TYPE(symbol.st_info);
    if (type == STT_GNU_IFUNC) return false;
    if (std::strcmp(strings + symbol.st_name, expected_symbol) != 0 ||
        symbol.st_shndx == SHN_UNDEF) continue;
    const unsigned binding = ELF64_ST_BIND(symbol.st_info);
    const unsigned visibility = ELF64_ST_VISIBILITY(symbol.st_other);
    if (type != STT_FUNC ||
        (binding != STB_GLOBAL && binding != STB_WEAK) ||
        (visibility != STV_DEFAULT && visibility != STV_PROTECTED)) return false;
    bool executable = false;
    for (const Elf64_Phdr* load : loads) {
      if (!(load->p_flags & PF_X) || symbol.st_value < load->p_vaddr) continue;
      const std::uint64_t delta = symbol.st_value - load->p_vaddr;
      if (delta < load->p_memsz) executable = true;
    }
    if (!executable) return false;
    ++expected_definitions;
  }
  if (expected_definitions != 1) return false;

  // Validate the loader's hash-derived symbol bounds.  This prevents a forged
  // sh_size from hiding symbols that dlsym can still reach beyond the claimed
  // canonical section.
  if (!hash_address.seen && !gnu_hash_address.seen) return false;
  if (hash_address.seen) {
    const std::uint64_t header_offset = virtual_to_offset(hash_address.value, 8);
    const std::uint32_t* hash_header =
        header_offset == UINT64_MAX ? nullptr : object_at<std::uint32_t>(image, header_offset, 2);
    if (!hash_header || hash_header[1] != symbol_count) return false;
    const std::uint64_t words = 2ULL + hash_header[0] + hash_header[1];
    if (words > SIZE_MAX / sizeof(std::uint32_t) ||
        virtual_to_offset(hash_address.value, words * sizeof(std::uint32_t)) ==
            UINT64_MAX) return false;
    const std::uint32_t* table = object_at<std::uint32_t>(
        image, header_offset, static_cast<std::size_t>(words));
    for (std::uint64_t i = 2; i < words; ++i) {
      if (table[i] >= symbol_count) return false;
    }
  }
  if (gnu_hash_address.seen) {
    const std::uint64_t header_offset =
        virtual_to_offset(gnu_hash_address.value, 4 * sizeof(std::uint32_t));
    const std::uint32_t* gnu_header = header_offset == UINT64_MAX
        ? nullptr : object_at<std::uint32_t>(image, header_offset, 4);
    if (!gnu_header || !gnu_header[0] || !gnu_header[2] ||
        gnu_header[1] > symbol_count) return false;
    const std::uint64_t prefix = 4ULL * sizeof(std::uint32_t) +
        static_cast<std::uint64_t>(gnu_header[2]) * sizeof(Elf64_Xword) +
        static_cast<std::uint64_t>(gnu_header[0]) * sizeof(std::uint32_t);
    const std::uint64_t chains = symbol_count - gnu_header[1];
    if (!chains || prefix > UINT64_MAX - chains * sizeof(std::uint32_t) ||
        virtual_to_offset(gnu_hash_address.value,
                          prefix + chains * sizeof(std::uint32_t)) == UINT64_MAX)
      return false;
    const std::uint64_t bucket_delta =
        4ULL * sizeof(std::uint32_t) +
        static_cast<std::uint64_t>(gnu_header[2]) * sizeof(Elf64_Xword);
    if (gnu_hash_address.value > UINT64_MAX - bucket_delta) return false;
    const std::uint64_t bucket_address = gnu_hash_address.value +
        bucket_delta;
    const std::uint64_t bucket_offset = virtual_to_offset(
        bucket_address,
        static_cast<std::uint64_t>(gnu_header[0]) * sizeof(std::uint32_t));
    const std::uint64_t bucket_bytes =
        static_cast<std::uint64_t>(gnu_header[0]) * sizeof(std::uint32_t);
    if (bucket_address > UINT64_MAX - bucket_bytes) return false;
    const std::uint64_t chain_address = bucket_address + bucket_bytes;
    const std::uint64_t chain_offset = virtual_to_offset(
        chain_address, chains * sizeof(std::uint32_t));
    const std::uint32_t* buckets = bucket_offset == UINT64_MAX ? nullptr :
        object_at<std::uint32_t>(image, bucket_offset, gnu_header[0]);
    const std::uint32_t* chain = chain_offset == UINT64_MAX ? nullptr :
        object_at<std::uint32_t>(image, chain_offset, static_cast<std::size_t>(chains));
    if (!buckets || (!chain && chains)) return false;
    std::uint64_t greatest_hashed_symbol = 0;
    bool saw_hashed_symbol = false;
    for (std::size_t i = 0; i < gnu_header[0]; ++i) {
      std::uint64_t index = buckets[i];
      if (!index) continue;
      if (index < gnu_header[1] || index >= symbol_count) return false;
      while (true) {
        const std::uint64_t chain_index = index - gnu_header[1];
        if (chain_index >= chains) return false;
        greatest_hashed_symbol = std::max(greatest_hashed_symbol, index);
        saw_hashed_symbol = true;
        if (chain[chain_index] & 1U) break;
        ++index;
      }
    }
    // GNU hash has no explicit nchain field.  Its canonical dynamic-symbol
    // extent ends at the greatest terminating bucket chain.  Requiring that
    // exact extent prevents a forged SHT_DYNSYM sh_size from hiding trailing
    // loader-visible symbols.
    if (!saw_hashed_symbol || greatest_hashed_symbol + 1 != symbol_count)
      return false;
  }

  auto safe_relocations = [&](const Relocations& table, bool with_addend) {
    if (!table.address.seen && !table.bytes.seen && !table.entry.seen) return true;
    const std::size_t expected = with_addend ? sizeof(Elf64_Rela) : sizeof(Elf64_Rel);
    if (!table.address.seen || !table.bytes.seen || !table.entry.seen ||
        !table.address.value || table.entry.value != expected ||
        table.bytes.value % expected) return false;
    if (!table.bytes.value) return true;
    const std::uint64_t offset =
        virtual_to_offset(table.address.value, table.bytes.value);
    if (offset == UINT64_MAX) return false;
    const std::size_t count = table.bytes.value / expected;
    for (std::size_t i = 0; i < count; ++i) {
      const std::uint64_t info = with_addend
          ? object_at<Elf64_Rela>(image, offset, count)[i].r_info
          : object_at<Elf64_Rel>(image, offset, count)[i].r_info;
      const std::uint32_t type = ELF64_R_TYPE(info);
      if (ELF64_R_SYM(info) >= symbol_count) return false;
#if defined(__aarch64__)
      if (type == R_AARCH64_IRELATIVE) return false;
#elif defined(__x86_64__)
      if (type == R_X86_64_IRELATIVE) return false;
#endif
    }
    return true;
  };
  if (!safe_relocations(rela, true) || !safe_relocations(rel, false)) return false;
  if (jump_address.seen || jump_bytes.seen || jump_type.seen) {
    if (!jump_address.seen || !jump_address.value || !jump_bytes.seen ||
        !jump_type.seen) return false;
    Relocations jump;
    jump.address = jump_address;
    jump.bytes = jump_bytes;
    jump.entry.seen = true;
    if (jump_type.value == DT_RELA) {
      jump.entry.value = sizeof(Elf64_Rela);
      if (!safe_relocations(jump, true)) return false;
    } else if (jump_type.value == DT_REL) {
      jump.entry.value = sizeof(Elf64_Rel);
      if (!safe_relocations(jump, false)) return false;
    } else {
      return false;
    }
  }
  return true;
}

inline bool attest_shared_object_path(const char* path, const char* expected_symbol) {
  const int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) return false;
  const bool okay = attest_shared_object_fd(fd, expected_symbol);
  close(fd);
  return okay;
}

inline void deny(std::vector<sock_filter>& p, int nr,
                 std::uint32_t action = SECCOMP_RET_ERRNO | EPERM) {
  p.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                       static_cast<std::uint32_t>(nr), 0, 1));
  p.push_back(BPF_STMT(BPF_RET | BPF_K, action));
}

inline void filter_open_flags(std::vector<sock_filter>& p, int nr,
                              unsigned argument) {
  constexpr std::uint32_t write_flags =
      O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND
#ifdef O_TMPFILE
      | O_TMPFILE
#endif
      ;
  p.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                       static_cast<std::uint32_t>(nr), 0, 3));
  p.push_back(BPF_STMT(
      BPF_LD | BPF_W | BPF_ABS,
      static_cast<std::uint32_t>(offsetof(struct seccomp_data, args) +
                                 argument * sizeof(std::uint64_t))));
  p.push_back(BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, write_flags, 0, 1));
  p.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM));
  p.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                       offsetof(struct seccomp_data, nr)));
}

inline void filter_control_fd(std::vector<sock_filter>& p, int nr,
                              int control_fd) {
  // Permit this socket operation only on the one trusted control channel.
  // The syscall-number accumulator is restored for subsequent rules.
  p.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                       static_cast<std::uint32_t>(nr), 0, 4));
  p.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                       offsetof(struct seccomp_data, args)));
  p.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                       static_cast<std::uint32_t>(control_fd), 1, 0));
  p.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM));
  p.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                       offsetof(struct seccomp_data, nr)));
}

inline void filter_clone(std::vector<sock_filter>& p, bool allow_threads) {
#ifdef __NR_clone3
  // glibc/libgomp fall back to clone(2), whose flags can be inspected.
  deny(p, __NR_clone3, SECCOMP_RET_ERRNO | ENOSYS);
#endif
#ifdef __NR_clone
  if (!allow_threads) {
    deny(p, __NR_clone);
    return;
  }
  constexpr std::uint32_t forbidden =
      CLONE_NEWCGROUP | CLONE_NEWIPC | CLONE_NEWNET | CLONE_NEWNS |
      CLONE_NEWPID | CLONE_NEWUSER | CLONE_NEWUTS | CLONE_PARENT |
      CLONE_PTRACE | CLONE_UNTRACED | CLONE_VFORK;
  p.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_clone, 0, 10));
  p.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                       offsetof(struct seccomp_data, args)));
  p.push_back(BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, CLONE_VM, 1, 0));
  p.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM));
  p.push_back(BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, CLONE_SIGHAND, 1, 0));
  p.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM));
  p.push_back(BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, CLONE_THREAD, 1, 0));
  p.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM));
  p.push_back(BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, forbidden, 0, 1));
  p.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM));
  p.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
  p.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                       offsetof(struct seccomp_data, nr)));
#endif
}

inline void filter_tgkill(std::vector<sock_filter>& p) {
#ifdef __NR_tgkill
  const std::uint32_t pid = static_cast<std::uint32_t>(syscall(__NR_getpid));
  const std::uint32_t tid = static_cast<std::uint32_t>(syscall(__NR_gettid));
  p.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_tgkill, 0, 10));
  p.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                       offsetof(struct seccomp_data, args)));
  p.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, pid, 1, 0));
  p.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM));
  p.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                       offsetof(struct seccomp_data, args) + 8));
  p.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, tid, 1, 0));
  p.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM));
  p.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                       offsetof(struct seccomp_data, args) + 16));
  p.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SIGSTOP, 1, 0));
  p.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM));
  p.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));
  p.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                       offsetof(struct seccomp_data, nr)));
#endif
}

inline void install_seccomp(bool loader_stage, bool allow_threads,
                            int control_fd = -1) {
  std::vector<sock_filter> p;
  p.reserve(256);
  p.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                       offsetof(struct seccomp_data, arch)));
  p.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, kAuditArch, 1, 0));
  p.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));
  p.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                       offsetof(struct seccomp_data, nr)));

#define CHEATSHEET_DENY(n) deny(p, n)
#ifdef __NR_write
  CHEATSHEET_DENY(__NR_write);
#endif
#ifdef __NR_writev
  CHEATSHEET_DENY(__NR_writev);
#endif
#ifdef __NR_pwrite64
  CHEATSHEET_DENY(__NR_pwrite64);
#endif
#ifdef __NR_pwritev
  CHEATSHEET_DENY(__NR_pwritev);
#endif
#ifdef __NR_sendfile
  CHEATSHEET_DENY(__NR_sendfile);
#endif
#ifdef __NR_copy_file_range
  CHEATSHEET_DENY(__NR_copy_file_range);
#endif
#ifdef __NR_splice
  CHEATSHEET_DENY(__NR_splice);
#endif
#ifdef __NR_vmsplice
  CHEATSHEET_DENY(__NR_vmsplice);
#endif

  if (loader_stage) {
#ifdef __NR_open
    filter_open_flags(p, __NR_open, 1);
#endif
#ifdef __NR_openat
    filter_open_flags(p, __NR_openat, 2);
#endif
  } else {
#ifdef __NR_open
    CHEATSHEET_DENY(__NR_open);
#endif
#ifdef __NR_openat
    CHEATSHEET_DENY(__NR_openat);
#endif
  }
#ifdef __NR_openat2
  CHEATSHEET_DENY(__NR_openat2);
#endif
#ifdef __NR_creat
  CHEATSHEET_DENY(__NR_creat);
#endif

  // Network, IPC and cross-process attack surface.
#ifdef __NR_socket
  CHEATSHEET_DENY(__NR_socket);
#endif
#ifdef __NR_socketpair
  CHEATSHEET_DENY(__NR_socketpair);
#endif
#ifdef __NR_connect
  CHEATSHEET_DENY(__NR_connect);
#endif
#ifdef __NR_bind
  CHEATSHEET_DENY(__NR_bind);
#endif
#ifdef __NR_listen
  CHEATSHEET_DENY(__NR_listen);
#endif
#ifdef __NR_accept
  CHEATSHEET_DENY(__NR_accept);
#endif
#ifdef __NR_accept4
  CHEATSHEET_DENY(__NR_accept4);
#endif
#ifdef __NR_sendto
  CHEATSHEET_DENY(__NR_sendto);
#endif
#ifdef __NR_sendmsg
  if (loader_stage && control_fd >= 0) filter_control_fd(p, __NR_sendmsg, control_fd);
  else CHEATSHEET_DENY(__NR_sendmsg);
#endif
#ifdef __NR_recvfrom
  CHEATSHEET_DENY(__NR_recvfrom);
#endif
#ifdef __NR_recvmsg
  if (loader_stage && control_fd >= 0) filter_control_fd(p, __NR_recvmsg, control_fd);
  else CHEATSHEET_DENY(__NR_recvmsg);
#endif
#ifdef __NR_kill
  CHEATSHEET_DENY(__NR_kill);
#endif
#ifdef __NR_tkill
  CHEATSHEET_DENY(__NR_tkill);
#endif
#ifdef __NR_ptrace
  CHEATSHEET_DENY(__NR_ptrace);
#endif
#ifdef __NR_process_vm_readv
  CHEATSHEET_DENY(__NR_process_vm_readv);
#endif
#ifdef __NR_process_vm_writev
  CHEATSHEET_DENY(__NR_process_vm_writev);
#endif
#ifdef __NR_pidfd_open
  CHEATSHEET_DENY(__NR_pidfd_open);
#endif
#ifdef __NR_pidfd_send_signal
  CHEATSHEET_DENY(__NR_pidfd_send_signal);
#endif
#ifdef __NR_fork
  CHEATSHEET_DENY(__NR_fork);
#endif
#ifdef __NR_vfork
  CHEATSHEET_DENY(__NR_vfork);
#endif
#ifdef __NR_execve
  CHEATSHEET_DENY(__NR_execve);
#endif
#ifdef __NR_execveat
  CHEATSHEET_DENY(__NR_execveat);
#endif
#ifdef __NR_pipe
  CHEATSHEET_DENY(__NR_pipe);
#endif
#ifdef __NR_pipe2
  CHEATSHEET_DENY(__NR_pipe2);
#endif
  filter_clone(p, loader_stage ? false : allow_threads);
  filter_tgkill(p);

  // Namespace/filesystem mutation and powerful kernel APIs.
#ifdef __NR_unshare
  CHEATSHEET_DENY(__NR_unshare);
#endif
#ifdef __NR_setns
  CHEATSHEET_DENY(__NR_setns);
#endif
#ifdef __NR_mount
  CHEATSHEET_DENY(__NR_mount);
#endif
#ifdef __NR_umount2
  CHEATSHEET_DENY(__NR_umount2);
#endif
#ifdef __NR_pivot_root
  CHEATSHEET_DENY(__NR_pivot_root);
#endif
#ifdef __NR_chroot
  CHEATSHEET_DENY(__NR_chroot);
#endif
#ifdef __NR_setsid
  CHEATSHEET_DENY(__NR_setsid);
#endif
#ifdef __NR_setpgid
  CHEATSHEET_DENY(__NR_setpgid);
#endif
#ifdef __NR_unlink
  CHEATSHEET_DENY(__NR_unlink);
#endif
#ifdef __NR_unlinkat
  CHEATSHEET_DENY(__NR_unlinkat);
#endif
#ifdef __NR_rename
  CHEATSHEET_DENY(__NR_rename);
#endif
#ifdef __NR_renameat
  CHEATSHEET_DENY(__NR_renameat);
#endif
#ifdef __NR_renameat2
  CHEATSHEET_DENY(__NR_renameat2);
#endif
#ifdef __NR_mkdir
  CHEATSHEET_DENY(__NR_mkdir);
#endif
#ifdef __NR_mkdirat
  CHEATSHEET_DENY(__NR_mkdirat);
#endif
#ifdef __NR_rmdir
  CHEATSHEET_DENY(__NR_rmdir);
#endif
#ifdef __NR_link
  CHEATSHEET_DENY(__NR_link);
#endif
#ifdef __NR_linkat
  CHEATSHEET_DENY(__NR_linkat);
#endif
#ifdef __NR_symlink
  CHEATSHEET_DENY(__NR_symlink);
#endif
#ifdef __NR_symlinkat
  CHEATSHEET_DENY(__NR_symlinkat);
#endif
#ifdef __NR_chmod
  CHEATSHEET_DENY(__NR_chmod);
#endif
#ifdef __NR_fchmod
  CHEATSHEET_DENY(__NR_fchmod);
#endif
#ifdef __NR_fchmodat
  CHEATSHEET_DENY(__NR_fchmodat);
#endif
#ifdef __NR_truncate
  CHEATSHEET_DENY(__NR_truncate);
#endif
#ifdef __NR_ftruncate
  CHEATSHEET_DENY(__NR_ftruncate);
#endif
#ifdef __NR_ioctl
  CHEATSHEET_DENY(__NR_ioctl);
#endif
#ifdef __NR_bpf
  CHEATSHEET_DENY(__NR_bpf);
#endif
#ifdef __NR_perf_event_open
  CHEATSHEET_DENY(__NR_perf_event_open);
#endif
#ifdef __NR_userfaultfd
  CHEATSHEET_DENY(__NR_userfaultfd);
#endif
#ifdef __NR_io_uring_setup
  CHEATSHEET_DENY(__NR_io_uring_setup);
#endif
#ifdef __NR_add_key
  CHEATSHEET_DENY(__NR_add_key);
#endif
#ifdef __NR_request_key
  CHEATSHEET_DENY(__NR_request_key);
#endif
#ifdef __NR_keyctl
  CHEATSHEET_DENY(__NR_keyctl);
#endif
#ifdef __NR_clock_settime
  CHEATSHEET_DENY(__NR_clock_settime);
#endif
#ifdef __NR_alarm
  CHEATSHEET_DENY(__NR_alarm);
#endif
#ifdef __NR_setitimer
  CHEATSHEET_DENY(__NR_setitimer);
#endif
#ifdef __NR_timer_create
  CHEATSHEET_DENY(__NR_timer_create);
#endif
#ifdef __NR_timer_settime
  CHEATSHEET_DENY(__NR_timer_settime);
#endif
#ifdef __NR_timerfd_create
  CHEATSHEET_DENY(__NR_timerfd_create);
#endif
#ifdef __NR_timerfd_settime
  CHEATSHEET_DENY(__NR_timerfd_settime);
#endif
#ifdef __NR_signalfd
  CHEATSHEET_DENY(__NR_signalfd);
#endif
#ifdef __NR_signalfd4
  CHEATSHEET_DENY(__NR_signalfd4);
#endif
#ifdef __NR_rt_sigqueueinfo
  CHEATSHEET_DENY(__NR_rt_sigqueueinfo);
#endif
#ifdef __NR_rt_tgsigqueueinfo
  CHEATSHEET_DENY(__NR_rt_tgsigqueueinfo);
#endif
#ifdef __NR_setrlimit
  CHEATSHEET_DENY(__NR_setrlimit);
#endif
#ifdef __NR_prlimit64
  CHEATSHEET_DENY(__NR_prlimit64);
#endif
#ifdef __NR_sched_setaffinity
  CHEATSHEET_DENY(__NR_sched_setaffinity);
#endif
#ifdef __NR_sched_setscheduler
  CHEATSHEET_DENY(__NR_sched_setscheduler);
#endif
#ifdef __NR_setpriority
  CHEATSHEET_DENY(__NR_setpriority);
#endif
  if (!loader_stage) {
#ifdef __NR_prctl
    CHEATSHEET_DENY(__NR_prctl);
#endif
#ifdef __NR_seccomp
    CHEATSHEET_DENY(__NR_seccomp);
#endif
  }
#undef CHEATSHEET_DENY
  p.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));

  struct sock_fprog program {
    static_cast<unsigned short>(p.size()), p.data()
  };
  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) _exit(121);
  if (syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER,
              SECCOMP_FILTER_FLAG_TSYNC, &program) != 0) _exit(122);
}

inline void block_all_signals() {
  sigset_t all;
  if (sigfillset(&all) != 0 || sigprocmask(SIG_SETMASK, &all, nullptr) != 0) {
    _exit(124);
  }
}

inline void sanitize_constructor_state() {
  block_all_signals();
  struct itimerval disabled {};
  (void)setitimer(ITIMER_REAL, &disabled, nullptr);
  (void)setitimer(ITIMER_VIRTUAL, &disabled, nullptr);
  (void)setitimer(ITIMER_PROF, &disabled, nullptr);

  struct sigaction action {};
  action.sa_handler = SIG_DFL;
  sigemptyset(&action.sa_mask);
  for (int signal = 1; signal < NSIG; ++signal) {
    if (signal != SIGKILL && signal != SIGSTOP) (void)sigaction(signal, &action, nullptr);
  }
  stack_t stack {};
  stack.ss_flags = SS_DISABLE;
  (void)sigaltstack(&stack, nullptr);

  struct timespec no_wait {};
  sigset_t pending;
  while (sigpending(&pending) == 0) {
    bool found = false;
    for (int signal = 1; signal < NSIG; ++signal) {
      if (sigismember(&pending, signal) == 1) { found = true; break; }
    }
    if (!found) break;
    if (sigtimedwait(&pending, nullptr, &no_wait) < 0 && errno == EAGAIN) break;
  }
}

inline void preinitialize_openmp_team() {
  omp_set_dynamic(0);
  int requested = omp_get_max_threads();
  requested = std::max(1, std::min(requested, 256));
  omp_set_num_threads(requested);
  volatile int participants = 0;
#pragma omp parallel num_threads(requested)
  {
#pragma omp atomic update
    participants += 1;
  }
  if (participants != requested) _exit(125);
}

inline void send_loaded(int control_fd) {
  char byte = 'L';
  struct iovec iov {&byte, sizeof(byte)};
  struct msghdr message {};
  message.msg_iov = &iov;
  message.msg_iovlen = 1;
  if (syscall(SYS_sendmsg, control_fd, &message, MSG_NOSIGNAL) != 1) _exit(126);
}

inline std::vector<int> receive_region_fds(int control_fd,
                                           std::size_t expected_count) {
  if (!expected_count || expected_count > 16) _exit(104);
  char byte = 0;
  struct iovec iov {&byte, sizeof(byte)};
  std::vector<char> ancillary(CMSG_SPACE(expected_count * sizeof(int)));
  struct msghdr message {};
  message.msg_iov = &iov;
  message.msg_iovlen = 1;
  message.msg_control = ancillary.data();
  message.msg_controllen = ancillary.size();
  const long received = syscall(SYS_recvmsg, control_fd, &message, MSG_CMSG_CLOEXEC);
  if (received != 1 || byte != 'F' || (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) {
    _exit(127);
  }
  std::vector<int> result;
  for (struct cmsghdr* header = CMSG_FIRSTHDR(&message); header;
       header = CMSG_NXTHDR(&message, header)) {
    if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS ||
        header->cmsg_len < CMSG_LEN(0)) _exit(127);
    const std::size_t bytes = header->cmsg_len - CMSG_LEN(0);
    if (bytes % sizeof(int)) _exit(127);
    const int* begin = reinterpret_cast<const int*>(CMSG_DATA(header));
    result.insert(result.end(), begin, begin + bytes / sizeof(int));
  }
  if (result.size() != expected_count) _exit(127);
  return result;
}

inline void send_region_fds(int control_fd, const std::vector<Region>& regions) {
  std::vector<int> fds;
  for (const Region& region : regions) fds.push_back(region.fd);
  char byte = 'F';
  struct iovec iov {&byte, sizeof(byte)};
  std::vector<char> ancillary(CMSG_SPACE(fds.size() * sizeof(int)));
  struct msghdr message {};
  message.msg_iov = &iov;
  message.msg_iovlen = 1;
  message.msg_control = ancillary.data();
  message.msg_controllen = ancillary.size();
  struct cmsghdr* header = CMSG_FIRSTHDR(&message);
  header->cmsg_level = SOL_SOCKET;
  header->cmsg_type = SCM_RIGHTS;
  header->cmsg_len = CMSG_LEN(fds.size() * sizeof(int));
  std::memcpy(CMSG_DATA(header), fds.data(), fds.size() * sizeof(int));
  message.msg_controllen = header->cmsg_len;
  if (sendmsg(control_fd, &message, MSG_NOSIGNAL) != 1) {
    throw std::runtime_error(std::string("send worker buffers: ") + std::strerror(errno));
  }
}

inline bool wait_for_loaded(int control_fd, pid_t pid, std::string* error) {
  struct pollfd descriptor {control_fd, POLLIN | POLLHUP, 0};
  int ready;
  do { ready = poll(&descriptor, 1, 30000); } while (ready < 0 && errno == EINTR);
  if (ready <= 0) {
    if (error) *error = ready == 0 ? "worker loader handshake timed out" :
        std::string("worker loader handshake: ") + std::strerror(errno);
    return false;
  }
  if (!(descriptor.revents & POLLIN) && !(descriptor.revents & POLLHUP)) {
    if (error) *error = "worker loader handshake returned an unexpected socket event";
    return false;
  }
  char byte = 0;
  const ssize_t count = (descriptor.revents & POLLIN)
      ? recv(control_fd, &byte, sizeof(byte), 0) : 0;
  if (count == 1 && byte == 'L') return true;
  // A closed control socket does not guarantee that the worker has exited.
  // Keep diagnostic collection bounded even for a broken loader.
  int status = 0;
  pid_t waited = 0;
  for (int attempt = 0; attempt < 100; ++attempt) {
    waited = waitpid(pid, &status, WNOHANG);
    if (waited == pid || (waited < 0 && errno != EINTR)) break;
    usleep(1000);
  }
  if (waited != pid) {
    if (error) *error = "worker failed before loader READY; exit status unavailable";
    return false;
  }
  if (error) {
    if (WIFEXITED(status)) {
      switch (WEXITSTATUS(status)) {
        case 106: *error = "worker could not open the submitted shared object"; break;
        case 109: *error = "worker could not exec the restricted loader"; break;
        case 110: *error = "dlopen failed (check shared-object dependencies/relocations)"; break;
        case 111: *error = "required exported kernel symbol is missing or mangled"; break;
        case 112: *error = "worker could not map the trusted input/output buffer"; break;
        case 113: *error = "shared object rejected by attestation (constructors/destructors, ELF layout, dependency, or relocation restriction)"; break;
        case 121: *error = "worker could not install the loader sandbox"; break;
        case 122: *error = "worker could not synchronize the loader sandbox"; break;
        default: *error = "worker exited before loader READY (exit " + std::to_string(WEXITSTATUS(status)) + ")"; break;
      }
    } else if (WIFSIGNALED(status)) {
      *error = "worker signal " + std::to_string(WTERMSIG(status)) + " during loader startup";
    } else {
      *error = "worker failed before loader READY";
    }
  }
  return false;
}

inline void stop_worker() {
  const long pid = syscall(__NR_getpid);
  const long tid = syscall(__NR_gettid);
  if (syscall(__NR_tgkill, pid, tid, SIGSTOP) != 0) _exit(123);
}

inline bool wait_for_stop(pid_t pid, std::string* error) {
  int status = 0;
  while (waitpid(pid, &status, WUNTRACED) < 0) {
    if (errno == EINTR) continue;
    if (error) *error = std::string("waitpid: ") + std::strerror(errno);
    return false;
  }
  if (WIFSTOPPED(status) && WSTOPSIG(status) == SIGSTOP) return true;
  if (error) {
    if (WIFEXITED(status)) *error = "worker exited " + std::to_string(WEXITSTATUS(status));
    else if (WIFSIGNALED(status)) {
      const int signal = WTERMSIG(status);
      if (signal == SIGSYS) *error = "worker hit a blocked syscall (sandbox restriction; signal SIGSYS)";
      else if (signal == SIGILL) *error = "worker executed an illegal instruction (signal SIGILL)";
      else if (signal == SIGSEGV || signal == SIGBUS) *error = "worker memory fault (signal " + std::to_string(signal) + ")";
      else if (signal == SIGABRT) *error = "worker aborted (possible sanitizer trap or runtime assertion)";
      else *error = "worker signal " + std::to_string(signal);
    }
    else *error = "worker missed stop boundary";
  }
  return false;
}

inline void kill_worker(pid_t pid) {
  if (pid <= 0) return;
  kill(pid, SIGKILL);
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
}

inline std::uint64_t parse_u64(const char* text) {
  char* end = nullptr;
  errno = 0;
  unsigned long long value = std::strtoull(text, &end, 10);
  if (errno || !text[0] || !end || *end) _exit(104);
  return static_cast<std::uint64_t>(value);
}

}  // namespace detail

inline bool is_worker_invocation(int argc, char** argv) {
  return argc >= 2 && std::strcmp(argv[1], "--cheatsheet-kernel-worker") == 0;
}

template <typename Function, typename Invoke>
int run_worker_invocation(int argc, char** argv, const char* expected_symbol,
                          bool allow_threads, Invoke invoke) {
  // argv: mode, so, symbol, control_fd, region_count, (bytes r|w)*, --, scalar...
  // No data descriptor number appears in argv and no data descriptor survives
  // exec.  They arrive atomically only after dlopen and constructor cleanup.
  if (argc < 7 || std::strcmp(argv[3], expected_symbol) != 0) return 104;
  const int control_fd = static_cast<int>(detail::parse_u64(argv[4]));
  const std::size_t region_count = static_cast<std::size_t>(detail::parse_u64(argv[5]));
  const std::size_t delimiter = 6 + region_count * 2;
  if (delimiter >= static_cast<std::size_t>(argc) ||
      std::strcmp(argv[delimiter], "--") != 0) return 104;

  struct Descriptor { std::size_t bytes; bool readonly; };
  std::vector<Descriptor> descriptors;
  for (std::size_t i = 0; i < region_count; ++i) {
    const std::size_t offset = 6 + i * 2;
    const std::size_t bytes = static_cast<std::size_t>(detail::parse_u64(argv[offset]));
    const bool readonly = std::strcmp(argv[offset + 1], "r") == 0;
    if (!bytes || (!readonly && std::strcmp(argv[offset + 1], "w") != 0)) return 104;
    descriptors.push_back({bytes, readonly});
  }
  if (control_fd < 3 || region_count == 0 || region_count > 16) return 104;

  detail::apply_limits();
  detail::close_except({control_fd});  // only the trusted loader sees this socket.
  detail::block_all_signals();
  if (allow_threads) detail::preinitialize_openmp_team();
  const int library_fd = open(argv[2], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (library_fd < 0) _exit(106);
  if (!detail::attest_shared_object_fd(library_fd, expected_symbol)) _exit(113);
  // TSYNC applies the loader policy to the already-created OpenMP team too.
  detail::install_seccomp(/*loader_stage=*/true, /*allow_threads=*/false,
                          control_fd);
  char library_fd_path[64];
  std::snprintf(library_fd_path, sizeof(library_fd_path), "/proc/self/fd/%d",
                library_fd);
  void* handle = dlopen(library_fd_path, RTLD_NOW | RTLD_LOCAL);
  close(library_fd);
  if (!handle) _exit(110);
  void* raw_function = dlsym(handle, expected_symbol);
  if (!raw_function) _exit(111);
  static_assert(sizeof(Function) == sizeof(raw_function), "unsupported function pointer ABI");
  Function function = nullptr;
  std::memcpy(&function, &raw_function, sizeof(function));

  // Defence in depth: attestation forbids constructors, but also reset any
  // mutable process state before announcing loader READY and receiving the
  // exact trusted buffers.
  detail::sanitize_constructor_state();
  detail::close_except({control_fd});
  detail::send_loaded(control_fd);
  std::vector<int> fds = detail::receive_region_fds(control_fd, region_count);
  std::vector<void*> mappings;
  for (std::size_t index = 0; index < descriptors.size(); ++index) {
    const Descriptor& descriptor = descriptors[index];
    int protection = PROT_READ | (descriptor.readonly ? 0 : PROT_WRITE);
    void* address = mmap(nullptr, descriptor.bytes, protection, MAP_SHARED,
                         fds[index], 0);
    if (address == MAP_FAILED) _exit(112);
    mappings.push_back(address);
  }
  detail::close_except({});
  // The strict policy is also synchronized to every OpenMP worker.  Ordinary
  // pthread-style clone flags remain available for libgomp replenishment.
  detail::install_seccomp(/*loader_stage=*/false,
                          /*allow_threads=*/false);

  std::vector<std::string> scalars;
  for (std::size_t i = delimiter + 1; i < static_cast<std::size_t>(argc); ++i) {
    scalars.emplace_back(argv[i]);
  }
  detail::stop_worker();  // READY: parent has not populated authoritative input.
  while (true) {
    invoke(function, mappings, scalars);
    detail::stop_worker();  // DONE: parent validates shared output itself.
  }
}

class Worker {
 public:
  Worker(const std::string& worker_path, const std::string& library_path,
         const std::string& symbol,
         const std::vector<Region>& regions,
         const std::vector<std::string>& scalar_arguments) {
    // Deny /proc/$PPID/{mem,environ,...} even on hosts with permissive ptrace
    // policy and even before the outer namespace's fresh procfs is considered.
    // This is irreversible for the lifetime of the trusted harness, by design.
    if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0) {
      throw std::runtime_error("cannot make trusted parent non-dumpable");
    }
    int control[2] = {-1, -1};
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, control) != 0) {
      throw std::runtime_error(std::string("create worker control socket: ") +
                               std::strerror(errno));
    }
    constexpr int kChildControlFd = 3;
    std::vector<std::string> arguments = {
        worker_path, "--cheatsheet-kernel-worker", library_path, symbol,
        std::to_string(kChildControlFd),
        std::to_string(regions.size())};
    for (const Region& region : regions) {
      if (region.fd < 3 || !region.bytes) throw std::invalid_argument("bad worker region");
      arguments.push_back(std::to_string(region.bytes));
      arguments.push_back(region.readonly_in_worker ? "r" : "w");
    }
    arguments.push_back("--");
    arguments.insert(arguments.end(), scalar_arguments.begin(), scalar_arguments.end());
    std::vector<char*> raw;
    for (std::string& argument : arguments) raw.push_back(argument.data());
    raw.push_back(nullptr);

    pid_ = fork();
    if (pid_ < 0) {
      close(control[0]);
      close(control[1]);
      throw std::runtime_error("fork worker failed");
    }
    if (pid_ == 0) {
      close(control[0]);
      if (control[1] != kChildControlFd) {
        if (dup2(control[1], kChildControlFd) < 0) _exit(107);
        close(control[1]);
      } else if (fcntl(kChildControlFd, F_SETFD, 0) != 0) {
        _exit(107);
      }
      detail::close_except({kChildControlFd});
      // execve preserves the environment by default.  Clear it before the new
      // image is created so submitted global constructors cannot read the API
      // key, proxy credentials, HOME, scheduler metadata, or parent markers.
      // OpenMP settings are copied through an explicit allowlist only.
      const char* allowed_names[] = {
          "OMP_NUM_THREADS", "OMP_PROC_BIND", "OMP_PLACES",
          "OMP_DYNAMIC"};
      std::vector<std::pair<std::string, std::string>> allowed_environment;
      for (const char* name : allowed_names) {
        const char* value = std::getenv(name);
        if (value) allowed_environment.emplace_back(name, value);
      }
      if (clearenv() != 0) _exit(108);
      for (const auto& item : allowed_environment) {
        if (setenv(item.first.c_str(), item.second.c_str(), 1) != 0) _exit(108);
      }
      execv(raw[0], raw.data());
      _exit(109);
    }
    close(control[1]);
    control_fd_ = control[0];
    if (!detail::wait_for_loaded(control_fd_, pid_, &error_)) {
      detail::kill_worker(pid_);
      close(control_fd_);
      control_fd_ = -1;
      pid_ = -1;
      throw std::runtime_error("worker failed before loader READY: " + error_);
    }
    try {
      detail::send_region_fds(control_fd_, regions);
    } catch (...) {
      detail::kill_worker(pid_);
      close(control_fd_);
      control_fd_ = -1;
      pid_ = -1;
      throw;
    }
    close(control_fd_);
    control_fd_ = -1;
    if (!detail::wait_for_stop(pid_, &error_)) {
      detail::kill_worker(pid_);
      pid_ = -1;
      throw std::runtime_error("worker failed before READY: " + error_);
    }
  }

  ~Worker() {
    if (control_fd_ >= 0) close(control_fd_);
    detail::kill_worker(pid_);
  }
  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;

  // Trusted parent only: bind perf_event_open to the already-loaded, stopped
  // worker leader. Neither this PID nor any perf fd crosses the control socket.
  pid_t pid() const noexcept { return pid_; }

  Invocation invoke(double overhead_ms = 0.0) {
    Invocation result;
    if (pid_ <= 0) { result.error = error_; return result; }
    const double begin = monotonic_seconds();
    if (kill(pid_, SIGCONT) != 0 || !detail::wait_for_stop(pid_, &result.error)) {
      detail::kill_worker(pid_);
      pid_ = -1;
      return result;
    }
    result.ok = true;
    result.elapsed_ms = std::max(
        0.0, (monotonic_seconds() - begin) * 1e3 - overhead_ms);
    return result;
  }

 private:
  pid_t pid_ = -1;
  int control_fd_ = -1;
  std::string error_;
};

inline double measure_resume_stop_overhead(int repetitions = 31) {
  pid_t child = fork();
  if (child < 0) throw std::runtime_error("fork overhead worker failed");
  if (child == 0) {
    detail::close_except({});
    detail::stop_worker();
    while (true) detail::stop_worker();
  }
  std::string error;
  if (!detail::wait_for_stop(child, &error)) {
    detail::kill_worker(child);
    throw std::runtime_error("overhead worker failed: " + error);
  }
  std::vector<double> samples;
  for (int i = 0; i < repetitions; ++i) {
    const double begin = monotonic_seconds();
    if (kill(child, SIGCONT) != 0 || !detail::wait_for_stop(child, &error)) {
      detail::kill_worker(child);
      throw std::runtime_error("overhead sample failed: " + error);
    }
    samples.push_back((monotonic_seconds() - begin) * 1e3);
  }
  detail::kill_worker(child);
  std::sort(samples.begin(), samples.end());
  return samples[samples.size() / 2];
}

// Run a trusted, potentially threaded correctness oracle in a disposable
// process.  In particular, libgomp keeps an OpenMP team alive after a parallel
// region returns.  Letting the trusted harness call such an oracle directly
// would leave those threads in the same process while the submitted worker is
// timed, where they can perturb CPU scheduling even though the oracle itself is
// outside the measured interval.
//
// The caller places the result in parent-created MAP_SHARED storage (for
// example SharedArray) captured by ``reference``.  Existing mappings survive
// fork, while every file descriptor is closed before the oracle runs.  The
// parent does not return until the whole helper process, including its OpenMP
// team, has exited and been reaped.
template <typename Function>
inline void run_trusted_reference_child(
    const std::vector<Region>& inaccessible_regions, Function&& reference) {
  const pid_t parent = getpid();
  const pid_t child = fork();
  if (child < 0) {
    throw std::runtime_error(std::string("fork trusted reference failed: ") +
                             std::strerror(errno));
  }
  if (child == 0) {
    if (prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) != 0 ||
        getppid() != parent) {
      _exit(128);
    }
    detail::apply_limits();
    // A fork inherits mappings as well as descriptors. Remove every buffer
    // shared with the submitted worker before running the oracle, then close
    // all descriptors. The separate result mapping captured by ``reference``
    // remains available; it is never included in the worker's region list.
    for (const Region& region : inaccessible_regions) {
      if (!region.address || !region.bytes ||
          munmap(region.address, region.bytes) != 0) {
        _exit(130);
      }
    }
    detail::close_except({});
    try {
      reference();
    } catch (...) {
      _exit(129);
    }
    _exit(0);
  }

  int status = 0;
  pid_t waited = -1;
  do {
    waited = waitpid(child, &status, 0);
  } while (waited < 0 && errno == EINTR);
  if (waited != child) {
    throw std::runtime_error(std::string("wait for trusted reference failed: ") +
                             std::strerror(errno));
  }
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return;
  if (WIFSIGNALED(status)) {
    throw std::runtime_error("trusted reference signal " +
                             std::to_string(WTERMSIG(status)));
  }
  if (WIFEXITED(status)) {
    throw std::runtime_error("trusted reference exited " +
                             std::to_string(WEXITSTATUS(status)));
  }
  throw std::runtime_error("trusted reference ended unexpectedly");
}

inline std::string submitted_library_path() {
  const char* path = std::getenv("CHEATSHEET_KERNEL_SO");
  if (!path || !*path) throw std::runtime_error("CHEATSHEET_KERNEL_SO is not set");
  return std::string(path);
}

inline std::string worker_executable_path() {
  const char* path = std::getenv("CHEATSHEET_KERNEL_WORKER");
  if (!path || !*path) throw std::runtime_error("CHEATSHEET_KERNEL_WORKER is not set");
  return std::string(path);
}

inline bool unchanged(const void* shared, const void* trusted, std::size_t bytes) {
  return std::memcmp(shared, trusted, bytes) == 0;
}

template <typename T>
inline void poison(T* output, std::size_t count) {
  std::memset(output, 0xa5, count * sizeof(T));
}

inline int parse_int(const std::string& value) {
  char* end = nullptr;
  long result = std::strtol(value.c_str(), &end, 10);
  if (!end || *end || result <= 0 || result > INT_MAX) _exit(105);
  return static_cast<int>(result);
}

}  // namespace isolated
