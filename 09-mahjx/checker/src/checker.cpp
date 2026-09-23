#include "checker.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <deque>
#include <istream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace full_analyze_checker {
namespace {

constexpr long double TOLERANCE = 1e-4L;

struct Line {
  std::size_t number;
  std::vector<std::string> fields;
};

struct PositionKey {
  long long trigger_event_id;
  int player;

  auto operator<=>(const PositionKey &) const = default;
};

auto number_error(double reference, double submission) -> long double {
  const long double reference_value = reference;
  const long double submission_value = submission;
  const long double absolute_error = std::abs(reference_value - submission_value);
  const long double scale = std::max(std::abs(reference_value), std::abs(submission_value));
  if (scale == 0) return absolute_error;
  return std::min(absolute_error, absolute_error / scale);
}

auto numbers_match(double reference, double submission) -> bool {
  return number_error(reference, submission) <= TOLERANCE;
}

auto record_error(ErrorMaximum *maximum, double reference, double submission) -> bool {
  const long double error = number_error(reference, submission);
  ++maximum->count;
  maximum->maximum = std::max(maximum->maximum, error);
  return error <= TOLERANCE;
}

auto split_fields(const std::string &text) -> std::vector<std::string> {
  std::vector<std::string> fields;
  std::size_t begin = 0;
  while (true) {
    const std::size_t end = text.find('\t', begin);
    fields.emplace_back(text.substr(begin, end - begin));
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return fields;
}

auto read_lines(std::istream &input) -> std::expected<std::vector<Line>, std::string> {
  std::vector<Line> lines;
  std::string text;
  for (std::size_t number = 1; std::getline(input, text); ++number) {
    if (text.empty()) {
      return std::unexpected("line " + std::to_string(number) +
                             ": empty records are not permitted");
    }
    if (text.back() == '\r') {
      return std::unexpected("line " + std::to_string(number) +
                             ": CR characters are not permitted");
    }
    if (input.eof()) {
      return std::unexpected("line " + std::to_string(number) +
                             ": the final record has no LF character");
    }
    auto fields = split_fields(text);
    if (std::ranges::any_of(fields,
                            [](const std::string &field) -> bool { return field.empty(); })) {
      return std::unexpected("line " + std::to_string(number) + ": empty fields are not permitted");
    }
    lines.push_back({number, std::move(fields)});
  }
  if (input.bad()) return std::unexpected("failed to read the input file");
  return lines;
}

template <typename Integer>
auto parse_integer(std::string_view text, Integer *value) -> bool {
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), *value);
  return error == std::errc{} && end == text.data() + text.size();
}

auto parse_number(std::string_view text, double *value) -> bool {
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), *value, std::chars_format::general);
  return error == std::errc{} && end == text.data() + text.size() && std::isfinite(*value);
}

auto is_tile(std::string_view tile) -> bool {
  if (tile == "?" || tile == "E" || tile == "S" || tile == "W" || tile == "N" || tile == "P" ||
      tile == "F" || tile == "C") {
    return true;
  }
  if (tile.size() != 2 && tile.size() != 3) return false;
  if (tile[0] < '1' || tile[0] > '9') return false;
  if (tile[1] != 'm' && tile[1] != 'p' && tile[1] != 's') return false;
  return tile.size() == 2 || (tile[0] == '5' && tile[2] == 'r');
}

auto actions_match(const Actions &left, const Actions &right) -> bool {
  if (left.size() != right.size()) return false;
  return std::ranges::equal(
      left, right, [](const Action &left_action, const Action &right_action) -> bool {
        if (left_action.fields.size() != right_action.fields.size()) return false;
        if (left_action.fields[0] != "DISCARD" || right_action.fields[0] != "DISCARD") {
          return left_action == right_action;
        }
        return std::ranges::equal(left_action.fields.begin(), left_action.fields.end() - 1,
                                  right_action.fields.begin(), right_action.fields.end() - 1);
      });
}

class OutputParser {
 public:
  explicit OutputParser(std::vector<Line> lines) : lines_(std::move(lines)) {}

  auto parse() -> std::expected<AnalyzeOutput, std::string> {
    AnalyzeOutput output;
    std::set<PositionKey> position_keys;
    std::deque<std::size_t> pending_positions;
    bool parsing_results = false;
    while (cursor_ < lines_.size()) {
      const Line &line = lines_[cursor_];
      if (line.fields[0] == "POS") {
        if (parsing_results && !pending_positions.empty()) {
          return std::unexpected("line " + std::to_string(line.number) +
                                 ": POS record interleaves pending ACTUAL records");
        }
        parsing_results = false;
        Position position;
        if (!parse_position_candidates(&position)) return std::unexpected(error_);
        const PositionKey key{position.trigger_event_id, position.player};
        if (!position_keys.emplace(key).second) {
          return std::unexpected("duplicate POS record for event " +
                                 std::to_string(key.trigger_event_id) + " and player " +
                                 std::to_string(key.player));
        }
        output.positions.push_back(std::move(position));
        pending_positions.push_back(output.positions.size() - 1);
        continue;
      }
      if (line.fields[0] == "ACTUAL") {
        if (pending_positions.empty()) {
          return std::unexpected("line " + std::to_string(line.number) +
                                 ": ACTUAL record has no pending POS record");
        }
        parsing_results = true;
        Position &position = output.positions[pending_positions.front()];
        if (!parse_position_result(&position)) {
          return std::unexpected("event " + std::to_string(position.trigger_event_id) +
                                 ", player " + std::to_string(position.player) + ": " + error_);
        }
        pending_positions.pop_front();
        continue;
      }
      return std::unexpected("line " + std::to_string(line.number) +
                             ": expected POS or ACTUAL record");
    }
    if (!pending_positions.empty()) {
      const Position &position = output.positions[pending_positions.front()];
      return std::unexpected("expected ACTUAL at end of file for event " +
                             std::to_string(position.trigger_event_id) + " and player " +
                             std::to_string(position.player));
    }
    return output;
  }

 private:
  auto next_line(std::string_view record) -> const Line * {
    if (cursor_ < lines_.size()) return &lines_[cursor_++];
    error_ = "expected " + std::string(record) + " at end of file";
    return nullptr;
  }

  auto require_record(const Line &line, std::string_view name, std::size_t field_count) -> bool {
    if (line.fields[0] != name || line.fields.size() != field_count) {
      error_ = "line " + std::to_string(line.number) + ": expected " + std::string(name) +
               " with " + std::to_string(field_count) + " fields";
      return false;
    }
    return true;
  }

  auto read_integer(const Line &line, std::size_t field, std::string_view name, auto *value)
      -> bool {
    if (parse_integer(line.fields[field], value)) return true;
    error_ = "line " + std::to_string(line.number) + ": invalid " + std::string(name);
    return false;
  }

  auto read_number(const Line &line, std::size_t field, std::string_view name, double *value)
      -> bool {
    if (parse_number(line.fields[field], value)) return true;
    error_ = "line " + std::to_string(line.number) + ": invalid " + std::string(name);
    return false;
  }

  auto read_optional_number(const Line &line, std::size_t field, std::string_view name,
                            std::optional<double> *value) -> bool {
    if (line.fields[field] == "-") {
      *value = std::nullopt;
      return true;
    }
    double number;
    if (!read_number(line, field, name, &number)) return false;
    *value = number;
    return true;
  }

  auto read_player(const Line &line, std::size_t field, std::string_view name) -> bool {
    int player;
    if (!read_integer(line, field, name, &player)) return false;
    if (player >= 0 && player < 4) return true;
    error_ =
        "line " + std::to_string(line.number) + ": " + std::string(name) + " is outside [0, 3]";
    return false;
  }

  auto read_tile(const Line &line, std::size_t field) -> bool {
    if (is_tile(line.fields[field])) return true;
    error_ = "line " + std::to_string(line.number) + ": invalid tile";
    return false;
  }

  auto parse_action(int expected_index, Action *action) -> bool {
    const Line *line = next_line("ACTION");
    if (line == nullptr) return false;
    if (line->fields.size() < 3 || line->fields[0] != "ACTION") {
      error_ = "line " + std::to_string(line->number) + ": expected ACTION record";
      return false;
    }

    int index;
    if (!read_integer(*line, 1, "action index", &index)) return false;
    if (index != expected_index) {
      error_ = "line " + std::to_string(line->number) + ": action index is not consecutive";
      return false;
    }

    const std::string &type = line->fields[2];
    std::size_t expected_fields = 0;
    if (type == "DRAW") expected_fields = 5;
    if (type == "DISCARD") expected_fields = 6;
    if (type == "RIICHI" || type == "RIICHI_ACCEPTED" || type == "DORA" ||
        type == "NINE_TERMINALS" || type == "PASS") {
      expected_fields = 4;
    }
    if (type == "CHII" || type == "PON") expected_fields = 8;
    if (type == "OPEN_KAN") expected_fields = 9;
    if (type == "CONCEALED_KAN" || type == "UPGRADED_KAN") {
      expected_fields = 8;
    }
    if (type == "WIN") expected_fields = 6;
    if (expected_fields == 0 || line->fields.size() != expected_fields) {
      error_ = "line " + std::to_string(line->number) + ": invalid ACTION fields";
      return false;
    }

    if (type == "DORA") {
      if (!read_tile(*line, 3)) return false;
    } else {
      if (!read_player(*line, 3, "player")) return false;
    }

    if (type == "DRAW" && !read_tile(*line, 4)) return false;
    if (type == "DISCARD") {
      if (!read_tile(*line, 4)) return false;
      int from_draw;
      if (!read_integer(*line, 5, "discard-from-draw value", &from_draw)) {
        return false;
      }
      if (from_draw != 0 && from_draw != 1) {
        error_ = "line " + std::to_string(line->number) + ": discard-from-draw value is not 0 or 1";
        return false;
      }
    }
    if (type == "CHII" || type == "PON" || type == "OPEN_KAN" || type == "WIN") {
      if (!read_player(*line, 4, "target")) return false;
      if (!read_tile(*line, 5)) return false;
      for (std::size_t field = 6; field < line->fields.size(); ++field) {
        if (!read_tile(*line, field)) return false;
      }
    }
    if (type == "CONCEALED_KAN") {
      for (std::size_t field = 4; field < line->fields.size(); ++field) {
        if (!read_tile(*line, field)) return false;
      }
    }
    if (type == "UPGRADED_KAN") {
      for (std::size_t field = 4; field < line->fields.size(); ++field) {
        if (!read_tile(*line, field)) return false;
      }
    }

    action->fields.assign(line->fields.begin() + 2, line->fields.end());
    return true;
  }

  auto parse_actions(int count, Actions *actions) -> bool {
    if (count < 0) {
      error_ = "negative action count";
      return false;
    }
    if (static_cast<std::size_t>(count) > lines_.size() - cursor_) {
      error_ = "action count is more than the remaining record count";
      return false;
    }
    actions->reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
      Action action;
      if (!parse_action(index, &action)) return false;
      actions->push_back(std::move(action));
    }
    return true;
  }

  auto parse_candidate(int expected_index, Candidate *candidate) -> bool {
    const Line *line = next_line("CAND");
    if (line == nullptr || !require_record(*line, "CAND", 7)) return false;
    if (!read_integer(*line, 1, "candidate index", &candidate->index)) {
      return false;
    }
    if (candidate->index != expected_index) {
      error_ = "line " + std::to_string(line->number) + ": candidate index is not consecutive";
      return false;
    }
    if (!read_number(*line, 2, "candidate score", &candidate->score)) {
      return false;
    }
    if (!read_optional_number(*line, 3, "points-after value", &candidate->points_after) ||
        !read_optional_number(*line, 4, "deal-in probability", &candidate->deal_in_probability) ||
        !read_optional_number(*line, 5, "deal-in weighted utility",
                              &candidate->deal_in_weighted_utility)) {
      return false;
    }
    int action_count;
    if (!read_integer(*line, 6, "action count", &action_count)) return false;
    return parse_actions(action_count, &candidate->actions);
  }

  auto parse_error(Position *position) -> bool {
    const Line *line = next_line("ERR_OK, ERR_UNMATCHED, or ERR_UNOBSERVED");
    if (line == nullptr) return false;
    if (line->fields[0] == "ERR_UNMATCHED") {
      if (!require_record(*line, "ERR_UNMATCHED", 1)) return false;
      position->error_status = ErrorStatus::unmatched;
      position->matched_error = std::nullopt;
      return true;
    }
    if (line->fields[0] == "ERR_UNOBSERVED") {
      if (!require_record(*line, "ERR_UNOBSERVED", 1)) return false;
      if (!position->actual_actions.empty()) {
        error_ = "ERR_UNOBSERVED requires an empty ACTUAL record";
        return false;
      }
      position->error_status = ErrorStatus::unobserved;
      position->matched_error = std::nullopt;
      return true;
    }
    if (!require_record(*line, "ERR_OK", 5)) return false;

    MatchedError matched_error;
    if (!read_integer(*line, 1, "actual candidate index", &matched_error.actual_candidate_index) ||
        !read_number(*line, 2, "best score", &matched_error.best_score) ||
        !read_number(*line, 3, "actual score", &matched_error.actual_score) ||
        !read_number(*line, 4, "error", &matched_error.error)) {
      return false;
    }
    if (matched_error.actual_candidate_index < 0 ||
        std::cmp_greater_equal(matched_error.actual_candidate_index, position->candidates.size())) {
      error_ = "line " + std::to_string(line->number) +
               ": actual candidate index is outside the candidate list";
      return false;
    }
    position->error_status = ErrorStatus::matched;
    position->matched_error = matched_error;
    return true;
  }

  auto validate_error(const Position &position) -> bool {
    if (position.error_status == ErrorStatus::unobserved) return true;
    const auto actual_candidate =
        std::ranges::find_if(position.candidates, [&](const Candidate &candidate) -> bool {
          return actions_match(candidate.actions, position.actual_actions);
        });
    if (position.error_status == ErrorStatus::unmatched) {
      if (actual_candidate == position.candidates.end()) return true;
      error_ = "ERR_UNMATCHED has a matching candidate";
      return false;
    }

    const MatchedError &matched_error = *position.matched_error;
    const Candidate &indexed_candidate =
        position.candidates[static_cast<std::size_t>(matched_error.actual_candidate_index)];
    if (!actions_match(indexed_candidate.actions, position.actual_actions)) {
      error_ = "ERR_OK points to a candidate that does not match ACTUAL";
      return false;
    }

    const double best_score =
        std::ranges::max_element(position.candidates, {}, &Candidate::score)->score;
    if (!numbers_match(best_score, matched_error.best_score) ||
        !numbers_match(indexed_candidate.score, matched_error.actual_score) ||
        !numbers_match(best_score - indexed_candidate.score, matched_error.error)) {
      error_ = "ERR_OK values do not match the candidate scores";
      return false;
    }
    return true;
  }

  auto parse_position_candidates(Position *position) -> bool {
    const Line *line = next_line("POS");
    if (line == nullptr || !require_record(*line, "POS", 4)) return false;
    if (!read_integer(*line, 1, "trigger event ID", &position->trigger_event_id) ||
        !read_integer(*line, 2, "player", &position->player)) {
      return false;
    }
    if (position->player < 0 || position->player >= 4) {
      error_ = "line " + std::to_string(line->number) + ": player is outside [0, 3]";
      return false;
    }

    int candidate_count;
    if (!read_integer(*line, 3, "candidate count", &candidate_count)) {
      return false;
    }
    if (candidate_count <= 0) {
      error_ = "line " + std::to_string(line->number) + ": candidate count is not positive";
      return false;
    }
    if (static_cast<std::size_t>(candidate_count) > lines_.size() - cursor_) {
      error_ = "line " + std::to_string(line->number) +
               ": candidate count is more than the remaining record count";
      return false;
    }
    position->candidates.reserve(static_cast<std::size_t>(candidate_count));
    std::set<Actions> candidate_actions;
    for (int index = 0; index < candidate_count; ++index) {
      Candidate candidate;
      if (!parse_candidate(index, &candidate)) return false;
      if (!candidate_actions.emplace(candidate.actions).second) {
        error_ = "duplicate candidate action sequence";
        return false;
      }
      position->candidates.push_back(std::move(candidate));
    }

    return true;
  }

  auto parse_position_result(Position *position) -> bool {
    const Line *line = next_line("ACTUAL");
    if (line == nullptr || !require_record(*line, "ACTUAL", 2)) return false;
    int action_count;
    if (!read_integer(*line, 1, "actual action count", &action_count)) {
      return false;
    }
    if (!parse_actions(action_count, &position->actual_actions)) return false;
    return parse_error(position) && validate_error(*position);
  }

  std::vector<Line> lines_;
  std::size_t cursor_ = 0;
  std::string error_;
};

auto position_name(const PositionKey &key) -> std::string {
  return "event " + std::to_string(key.trigger_event_id) + ", player " + std::to_string(key.player);
}

auto compare_optional_number(const std::optional<double> &reference,
                             const std::optional<double> &submission, std::string_view name,
                             ErrorMaximum *maximum) -> std::optional<std::string> {
  if (reference.has_value() != submission.has_value()) {
    return std::string(name) + " presence differs";
  }
  if (reference && !record_error(maximum, *reference, *submission)) {
    return std::string(name) + " differs";
  }
  return std::nullopt;
}

}  // namespace

auto parse_output(std::istream &input) -> std::expected<AnalyzeOutput, std::string> {
  auto lines = read_lines(input);
  if (!lines) return std::unexpected(std::move(lines).error());
  return OutputParser(std::move(*lines)).parse();
}

auto compare_outputs(const AnalyzeOutput &reference, const AnalyzeOutput &submission)
    -> std::expected<ComparisonStatistics, std::string> {
  ComparisonStatistics statistics;
  std::map<PositionKey, const Position *> reference_positions;
  std::map<PositionKey, const Position *> submission_positions;
  for (const Position &position : reference.positions) {
    reference_positions.emplace(PositionKey{position.trigger_event_id, position.player}, &position);
  }
  for (const Position &position : submission.positions) {
    submission_positions.emplace(PositionKey{position.trigger_event_id, position.player},
                                 &position);
  }
  if (reference_positions.size() != submission_positions.size()) {
    return std::unexpected("the position count differs");
  }

  for (const auto &[key, reference_position] : reference_positions) {
    const auto submission_position_iterator = submission_positions.find(key);
    if (submission_position_iterator == submission_positions.end()) {
      return std::unexpected(position_name(key) + ": position is missing");
    }
    const Position &submission_position = *submission_position_iterator->second;
    if (reference_position->actual_actions != submission_position.actual_actions) {
      return std::unexpected(position_name(key) + ": ACTUAL actions differ");
    }

    std::map<Actions, const Candidate *> reference_candidates;
    std::map<Actions, const Candidate *> submission_candidates;
    for (const Candidate &candidate : reference_position->candidates) {
      reference_candidates.emplace(candidate.actions, &candidate);
    }
    for (const Candidate &candidate : submission_position.candidates) {
      submission_candidates.emplace(candidate.actions, &candidate);
    }
    if (reference_candidates.size() != submission_candidates.size()) {
      return std::unexpected(position_name(key) + ": candidate count differs");
    }

    for (const auto &[actions, reference_candidate] : reference_candidates) {
      const auto submission_candidate_iterator = submission_candidates.find(actions);
      if (submission_candidate_iterator == submission_candidates.end()) {
        return std::unexpected(position_name(key) + ": a candidate is missing");
      }
      const Candidate &submission_candidate = *submission_candidate_iterator->second;
      if (!record_error(&statistics.candidate_score, reference_candidate->score,
                        submission_candidate.score)) {
        return std::unexpected(position_name(key) + ": candidate score differs");
      }
      if (auto error = compare_optional_number(reference_candidate->points_after,
                                               submission_candidate.points_after,
                                               "points-after value", &statistics.points_after)) {
        return std::unexpected(position_name(key) + ": " + *error);
      }
      if (auto error = compare_optional_number(
              reference_candidate->deal_in_probability, submission_candidate.deal_in_probability,
              "deal-in probability", &statistics.deal_in_probability)) {
        return std::unexpected(position_name(key) + ": " + *error);
      }
      if (auto error = compare_optional_number(reference_candidate->deal_in_weighted_utility,
                                               submission_candidate.deal_in_weighted_utility,
                                               "deal-in weighted utility",
                                               &statistics.deal_in_weighted_utility)) {
        return std::unexpected(position_name(key) + ": " + *error);
      }
    }

    if (reference_position->error_status != submission_position.error_status) {
      return std::unexpected(position_name(key) + ": ERR status differs");
    }
    if (reference_position->error_status != ErrorStatus::matched) continue;

    const MatchedError &reference_error = *reference_position->matched_error;
    const MatchedError &submission_error = *submission_position.matched_error;
    const Actions &reference_error_actions =
        reference_position
            ->candidates[static_cast<std::size_t>(reference_error.actual_candidate_index)]
            .actions;
    const Actions &submission_error_actions =
        submission_position
            .candidates[static_cast<std::size_t>(submission_error.actual_candidate_index)]
            .actions;
    if (reference_error_actions != submission_error_actions) {
      return std::unexpected(position_name(key) + ": ERR actual candidate differs");
    }
    if (!record_error(&statistics.error_best_score, reference_error.best_score,
                      submission_error.best_score) ||
        !record_error(&statistics.error_actual_score, reference_error.actual_score,
                      submission_error.actual_score) ||
        !record_error(&statistics.error, reference_error.error, submission_error.error)) {
      return std::unexpected(position_name(key) + ": ERR values differ");
    }
  }
  return statistics;
}

}  // namespace full_analyze_checker
