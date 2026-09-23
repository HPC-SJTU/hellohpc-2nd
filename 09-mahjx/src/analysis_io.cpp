#include "analysis_io.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "share/types.hpp"

namespace {
void field(std::ostringstream &out, int value) { out << '\t' << value; }
void field(std::ostringstream &out, const std::string &value) { out << '\t' << value; }
std::string tile(int value) { return value < 0 ? "?" : tile_string(value); }

std::string number(float value) {
  if (!std::isfinite(value)) throw std::runtime_error("analysis float is not finite");
  if (value == 0.0f) return "0";
  std::ostringstream out;
  out << std::setprecision(9) << value;
  std::string text = out.str();
  const size_t exponent = text.find_first_of("eE");
  if (exponent != std::string::npos) {
    size_t digits = exponent + 1;
    if (digits < text.size() && (text[digits] == '+' || text[digits] == '-')) ++digits;
    while (digits + 1 < text.size() && text[digits] == '0') text.erase(digits, 1);
  }
  return text;
}

void metric(std::ostringstream &out, const Decision_Metric &value) {
  field(out, value.present ? number(value.value) : "-");
}

void action(std::ostringstream &out, int index, const Event &source) {
  Event e = source;
  std::sort(e.consumed.begin(), e.consumed.end(), [](int left, int right) -> bool {
    const int left_order = left % 10 == 0 ? 40 + left / 10 : left;
    const int right_order = right % 10 == 0 ? 40 + right / 10 : right;
    return left_order < right_order;
  });
  out << "ACTION";
  field(out, index);
  switch (e.type) {
    case EventType::DRAW:
      field(out, "DRAW");
      field(out, e.player);
      field(out, tile(e.tile));
      break;
    case EventType::DISCARD:
      field(out, "DISCARD");
      field(out, e.player);
      field(out, tile(e.tile));
      field(out, e.from_draw ? 1 : 0);
      break;
    case EventType::RIICHI:
      field(out, "RIICHI");
      field(out, e.player);
      break;
    case EventType::RIICHI_ACCEPTED:
      field(out, "RIICHI_ACCEPTED");
      field(out, e.player);
      break;
    case EventType::DORA:
      field(out, "DORA");
      field(out, tile(e.dora_indicator));
      break;
    case EventType::CHII:
    case EventType::PON:
    case EventType::OPEN_KAN:
      field(out, e.type == EventType::CHII  ? "CHII"
                 : e.type == EventType::PON ? "PON"
                                            : "OPEN_KAN");
      field(out, e.player);
      field(out, e.target);
      field(out, tile(e.tile));
      for (int x : e.consumed) field(out, tile(x));
      break;
    case EventType::CONCEALED_KAN:
      field(out, "CONCEALED_KAN");
      field(out, e.player);
      for (int x : e.consumed) field(out, tile(x));
      break;
    case EventType::UPGRADED_KAN:
      field(out, "UPGRADED_KAN");
      field(out, e.player);
      field(out, tile(e.tile));
      for (int x : e.consumed) field(out, tile(x));
      break;
    case EventType::WIN:
      field(out, "WIN");
      field(out, e.player);
      field(out, e.target);
      field(out, tile(e.tile));
      break;
    case EventType::NINE_TERMINALS:
      field(out, "NINE_TERMINALS");
      field(out, e.player);
      break;
    case EventType::PASS:
      field(out, "PASS");
      field(out, e.player);
      break;
    default:
      throw std::runtime_error("invalid decision action");
  }
}

void actions(std::ostringstream &out, const Moves &moves) {
  for (size_t i = 0; i < moves.size(); ++i) {
    if (i) out << '\n';
    action(out, static_cast<int>(i), moves[i]);
  }
}

void candidates(std::ostringstream &out, const Analysis_Position &position) {
  out << "POS";
  field(out, position.trigger_eid);
  field(out, position.player);
  field(out, static_cast<int>(position.candidates.size()));
  for (size_t i = 0; i < position.candidates.size(); ++i) {
    const Decision &candidate = position.candidates[i];
    out << '\n' << "CAND";
    field(out, static_cast<int>(i));
    field(out, number(candidate.score));
    metric(out, candidate.pt_exp_after);
    metric(out, candidate.total_deal_in_tile_prob_now);
    metric(out, candidate.total_deal_in_tile_weighted_utility_now);
    field(out, static_cast<int>(candidate.actions.size()));
    if (!candidate.actions.empty()) {
      out << '\n';
      actions(out, candidate.actions);
    }
  }
}
}  // namespace

std::string canonical_action_text(const Moves &moves) {
  std::ostringstream out;
  actions(out, moves);
  return out.str();
}

std::string write_action_records(const Moves &moves) {
  std::ostringstream out;
  actions(out, moves);
  return out.str();
}

std::string write_candidate_actions(const Moves &moves, int index) {
  std::ostringstream out;
  out << "CAND_ACTIONS";
  field(out, index);
  field(out, static_cast<int>(moves.size()));
  if (!moves.empty()) {
    out << '\n';
    actions(out, moves);
  }
  return out.str();
}

std::string write_legal_decisions(const std::vector<Moves> &decisions) {
  std::ostringstream out;
  out << "LEGAL";
  field(out, static_cast<int>(decisions.size()));
  for (size_t i = 0; i < decisions.size(); ++i) {
    out << '\n' << write_candidate_actions(decisions[i], static_cast<int>(i));
  }
  return out.str();
}

std::string write_position_candidates(const Analysis_Position &position) {
  std::ostringstream out;
  candidates(out, position);
  return out.str();
}

std::string write_position_result(const Analysis_Position &position, bool unobserved) {
  std::ostringstream out;
  out << "ACTUAL";
  field(out, unobserved ? 0 : static_cast<int>(position.actual.size()));
  if (!unobserved && !position.actual.empty()) {
    out << '\n';
    actions(out, position.actual);
  }
  if (unobserved)
    out << "\nERR_UNOBSERVED";
  else if (position.actual_candidate < 0)
    out << "\nERR_UNMATCHED";
  else {
    float best = position.candidates[0].score;
    for (const Decision &d : position.candidates) best = std::max(best, d.score);
    const float actual = position.candidates[position.actual_candidate].score;
    out << "\nERR_OK";
    field(out, position.actual_candidate);
    field(out, number(best));
    field(out, number(actual));
    field(out, number(best - actual));
  }
  return out.str();
}
