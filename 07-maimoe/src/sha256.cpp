#include "maimoe/sha256.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <utility>

namespace maimoe {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

[[nodiscard]] std::uint32_t load_be32(const std::uint8_t* input) noexcept {
    return (static_cast<std::uint32_t>(input[0]) << 24U) |
           (static_cast<std::uint32_t>(input[1]) << 16U) |
           (static_cast<std::uint32_t>(input[2]) << 8U) |
           static_cast<std::uint32_t>(input[3]);
}

void store_be32(std::uint8_t* output, std::uint32_t value) noexcept {
    output[0] = static_cast<std::uint8_t>(value >> 24U);
    output[1] = static_cast<std::uint8_t>(value >> 16U);
    output[2] = static_cast<std::uint8_t>(value >> 8U);
    output[3] = static_cast<std::uint8_t>(value);
}

[[nodiscard]] int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

}

FormatError::FormatError(std::string message, std::size_t line, std::size_t column)
    : std::runtime_error(std::move(message)), line_(line), column_(column) {}

void append_lp32(Bytes& out, std::span<const std::uint8_t> value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("LP32 value is too large");
    }
    append_le(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

void append_lp32(Bytes& out, std::string_view value) {
    append_lp32(out, std::span<const std::uint8_t>(
                         reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
}

Sha256::Sha256()
    : state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
             0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U} {}

void Sha256::transform(const std::uint8_t* block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i) {
        words[i] = load_be32(block + i * 4U);
    }
    for (std::size_t i = 16; i < words.size(); ++i) {
        const std::uint32_t s0 = std::rotr(words[i - 15], 7) ^
                                 std::rotr(words[i - 15], 18) ^ (words[i - 15] >> 3U);
        const std::uint32_t s1 = std::rotr(words[i - 2], 17) ^
                                 std::rotr(words[i - 2], 19) ^ (words[i - 2] >> 10U);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::uint32_t sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const std::uint32_t choose = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + sum1 + choose + kRoundConstants[i] + words[i];
        const std::uint32_t sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(std::span<const std::uint8_t> bytes) {
    if (finished_) {
        throw std::logic_error("SHA-256 context is already finished");
    }
    constexpr std::uint64_t max_bytes = std::numeric_limits<std::uint64_t>::max() / 8U;
    if (total_bytes_ > max_bytes || bytes.size() > max_bytes - total_bytes_) {
        throw std::length_error("SHA-256 input is too large");
    }
    total_bytes_ += static_cast<std::uint64_t>(bytes.size());

    while (!bytes.empty()) {
        const std::size_t count = std::min(buffer_.size() - buffered_, bytes.size());
        std::copy_n(bytes.begin(), count, buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_));
        buffered_ += count;
        bytes = bytes.subspan(count);
        if (buffered_ == buffer_.size()) {
            transform(buffer_.data());
            buffered_ = 0;
        }
    }
}

void Sha256::update(std::string_view text) {
    update(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

Digest Sha256::finish() {
    if (finished_) {
        throw std::logic_error("SHA-256 context is already finished");
    }
    finished_ = true;
    const std::uint64_t bit_length = total_bytes_ * 8U;
    buffer_[buffered_++] = 0x80U;
    if (buffered_ > 56U) {
        std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_), buffer_.end(),
                  std::uint8_t{0});
        transform(buffer_.data());
        buffered_ = 0;
    }
    std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_), buffer_.begin() + 56,
              std::uint8_t{0});
    for (std::size_t i = 0; i < 8; ++i) {
        buffer_[63U - i] = static_cast<std::uint8_t>(bit_length >> (8U * i));
    }
    transform(buffer_.data());

    Digest digest{};
    for (std::size_t i = 0; i < state_.size(); ++i) {
        store_be32(digest.data() + i * 4U, state_[i]);
    }
    return digest;
}

Digest sha256(std::span<const std::uint8_t> bytes) {
    Sha256 hasher;
    hasher.update(bytes);
    return hasher.finish();
}

Digest sha256(std::string_view text) {
    Sha256 hasher;
    hasher.update(text);
    return hasher.finish();
}

std::string digest_hex(const Digest& digest) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.resize(digest.size() * 2U);
    for (std::size_t i = 0; i < digest.size(); ++i) {
        result[i * 2U] = digits[digest[i] >> 4U];
        result[i * 2U + 1U] = digits[digest[i] & 0x0fU];
    }
    return result;
}

Digest parse_digest_hex(std::string_view text) {
    if (text.size() != 64U) {
        throw FormatError("SHA-256 must contain exactly 64 lowercase hex digits");
    }
    Digest result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        const int high = hex_value(text[i * 2U]);
        const int low = hex_value(text[i * 2U + 1U]);
        if (high < 0 || low < 0) {
            throw FormatError("SHA-256 contains a non-lowercase-hex character");
        }
        result[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return result;
}

}
