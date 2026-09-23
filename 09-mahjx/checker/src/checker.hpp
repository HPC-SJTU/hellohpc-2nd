#pragma once

#include <cstddef>
#include <expected>
#include <istream>
#include <optional>
#include <string>
#include <vector>

namespace full_analyze_checker {

struct Action {
  std::vector<std::string> fields;

  auto operator<=>(const Action &) const = default;
};

using Actions = std::vector<Action>;

struct Candidate {
  int index;
  double score;
  std::optional<double> points_after;
  std::optional<double> deal_in_probability;
  std::optional<double> deal_in_weighted_utility;
  Actions actions;
};

struct MatchedError {
  int actual_candidate_index;
  double best_score;
  double actual_score;
  double error;
};

enum class ErrorStatus { matched, unmatched, unobserved };

struct Position {
  long long trigger_event_id;
  int player;
  std::vector<Candidate> candidates;
  Actions actual_actions;
  ErrorStatus error_status;
  std::optional<MatchedError> matched_error;
};

struct AnalyzeOutput {
  std::vector<Position> positions;
};

struct ErrorMaximum {
  std::size_t count = 0;
  long double maximum = 0;
};

struct ComparisonStatistics {
  ErrorMaximum candidate_score;
  ErrorMaximum points_after;
  ErrorMaximum deal_in_probability;
  ErrorMaximum deal_in_weighted_utility;
  ErrorMaximum error_best_score;
  ErrorMaximum error_actual_score;
  ErrorMaximum error;
};

auto parse_output(std::istream &input) -> std::expected<AnalyzeOutput, std::string>;
auto compare_outputs(const AnalyzeOutput &reference, const AnalyzeOutput &submission)
    -> std::expected<ComparisonStatistics, std::string>;

}  // namespace full_analyze_checker
