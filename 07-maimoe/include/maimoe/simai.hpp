#pragma once

#include "maimoe/types.hpp"

#include <string>
#include <string_view>

namespace maimoe {

[[nodiscard]] ParsedChart parse_maidata(std::string_view text);
[[nodiscard]] std::string write_maidata(const ParsedChart& chart);
[[nodiscard]] const SemanticError* select_semantic_error(const ParsedChart& chart) noexcept;

}
