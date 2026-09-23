#pragma once

#include "maimoe/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace maimoe {

class Sha256 {
public:
    Sha256();

    void update(std::span<const std::uint8_t> bytes);
    void update(std::string_view text);
    [[nodiscard]] Digest finish();

private:
    void transform(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t total_bytes_ = 0;
    std::size_t buffered_ = 0;
    bool finished_ = false;
};

[[nodiscard]] Digest sha256(std::span<const std::uint8_t> bytes);
[[nodiscard]] Digest sha256(std::string_view text);
[[nodiscard]] std::string digest_hex(const Digest& digest);
[[nodiscard]] Digest parse_digest_hex(std::string_view text);

}
