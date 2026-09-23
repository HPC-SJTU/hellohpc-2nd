#include "streaming_analyzer.hpp"

#include <chrono>
#include <iomanip>
#include <istream>
#include <ostream>
#include <stdexcept>
#include <string>

#include "analysis_io.hpp"
#include "share/record_io.hpp"

namespace {
bool is_side_effect(const Event &event) {
  return event.type == EventType::DORA || event.type == EventType::RIICHI_ACCEPTED;
}

bool is_response(const Event &event) {
  return event.type == EventType::CHII || event.type == EventType::PON ||
         event.type == EventType::OPEN_KAN || event.type == EventType::WIN;
}

bool is_boundary(const Event &event) {
  return event.type == EventType::DRAW || is_round_result(event.type) ||
         event.type == EventType::ROUND || event.type == EventType::END_GAME;
}

void match_actual(Analysis_Position *position) {
  for (size_t i = 0; i < position->candidates.size(); ++i) {
    if (analysis_same_moves(position->actual, position->candidates[i].actions)) {
      position->actual_candidate = static_cast<int>(i);
      return;
    }
  }
}
}  // namespace

StreamingAnalyzer::StreamingAnalyzer(const int player_mask, const Reviewer &reviewer,
                                     const PositionObserver &review_observer,
                                     const PositionObserver &candidate_observer,
                                     const ResultObserver &result_observer)
    : player_mask_(player_mask),
      reviewer_(reviewer),
      review_observer_(review_observer),
      candidate_observer_(candidate_observer),
      result_observer_(result_observer) {
  if (player_mask < 1 || player_mask > 15) throw std::invalid_argument("player mask must be 1..15");
  if (!reviewer_) throw std::invalid_argument("reviewer is required");
}

std::vector<Streaming_Result> StreamingAnalyzer::advance(const Event &event) {
  std::vector<Streaming_Result> out;
  for (size_t g = 0; g < groups_.size();) {
    PendingGroup &group = groups_[g];
    if (is_side_effect(event)) {
      ++g;
      continue;
    }
    if (group.waiting_discard) {
      if (event.type != EventType::DISCARD || event.player != group.compound_player)
        throw std::runtime_error("compound action must be followed by same-player DISCARD");
      for (Analysis_Position &position : group.positions) {
        if (group.trigger.type == EventType::DRAW || position.player == group.compound_player) {
          position.actual.push_back(group.compound_action);
          position.actual.push_back(event);
          match_actual(&position);
          out.push_back(Streaming_Result{position, false});
        } else {
          out.push_back(Streaming_Result{position, true});
        }
      }
      groups_.erase(groups_.begin() + g);
      continue;
    }

    if (group.trigger.type == EventType::DRAW) {
      const int player = group.trigger.player;
      if (event.player == player &&
          (event.type == EventType::DISCARD || event.type == EventType::CONCEALED_KAN ||
           event.type == EventType::UPGRADED_KAN || event.type == EventType::WIN)) {
        for (Analysis_Position &position : group.positions) {
          position.actual.push_back(event);
          match_actual(&position);
          out.push_back(Streaming_Result{position, false});
        }
        groups_.erase(groups_.begin() + g);
        continue;
      }
      if (event.type == EventType::RIICHI && event.player == player) {
        group.waiting_discard = true;
        group.compound_player = player;
        group.compound_action = event;
        ++g;
        continue;
      }
      if (event.type == EventType::DRAWN_NINE_TERMINALS && event.player == player) {
        Event actual;
        actual.type = EventType::NINE_TERMINALS;
        actual.player = player;
        for (Analysis_Position &position : group.positions) {
          position.actual.push_back(actual);
          match_actual(&position);
          out.push_back(Streaming_Result{position, false});
        }
        groups_.erase(groups_.begin() + g);
        continue;
      }
      if (is_boundary(event)) throw std::runtime_error("DRAW decision missing actual action");
      ++g;
      continue;
    }

    if (is_response(event)) {
      if (event.type == EventType::CHII || event.type == EventType::PON) {
        group.waiting_discard = true;
        group.compound_player = event.player;
        group.compound_action = event;
        ++g;
        continue;
      }
      for (Analysis_Position &position : group.positions) {
        if (position.player == event.player) {
          position.actual.push_back(event);
          match_actual(&position);
          out.push_back(Streaming_Result{position, false});
        } else {
          out.push_back(Streaming_Result{position, true});
        }
      }
      groups_.erase(groups_.begin() + g);
      continue;
    }
    if (is_boundary(event)) {
      for (Analysis_Position &position : group.positions) {
        Event pass;
        pass.type = EventType::PASS;
        pass.player = position.player;
        position.actual.push_back(pass);
        match_actual(&position);
        out.push_back(Streaming_Result{position, false});
      }
      groups_.erase(groups_.begin() + g);
      continue;
    }
    ++g;
  }
  return out;
}

void StreamingAnalyzer::create_group(const Moves &record, const Event &trigger) {
  const bool draw = trigger.type == EventType::DRAW;
  const bool response =
      trigger.type == EventType::DISCARD || trigger.type == EventType::UPGRADED_KAN;
  if (!draw && !response) return;
  PendingGroup group;
  group.trigger = trigger;
  for (int player = 0; player < 4; ++player) {
    if (!(player_mask_ & (1 << player)) ||
        (draw ? player != trigger.player : player == trigger.player))
      continue;
    Analysis_Position position = reviewer_(record, player, trigger.eid);
    if (review_observer_) review_observer_(position);
    if (position.candidates.size() >= 2) {
      if (candidate_observer_) candidate_observer_(position);
      group.positions.push_back(position);
    }
  }
  if (!group.positions.empty()) groups_.push_back(group);
}

std::vector<Streaming_Result> StreamingAnalyzer::consume(const Moves &record) {
  if (record.empty()) throw std::invalid_argument("empty record prefix");
  const Event &event = record.back();
  std::vector<Streaming_Result> out = advance(event);
  if (result_observer_)
    for (const Streaming_Result &result : out) result_observer_(result);
  create_group(record, event);
  return out;
}

void StreamingAnalyzer::finish() const {
  if (!groups_.empty()) throw std::runtime_error("pending analysis at EOF");
}

void run_full_analyze_protocol(std::istream &input, std::ostream &output, std::ostream &markers,
                               const int player_mask, const StreamingAnalyzer::Reviewer &reviewer) {
  RecordParser parser;
  Moves record;
  std::string line;
  typedef std::chrono::steady_clock Clock;
  const Clock::time_point analysis_start = Clock::now();
  Clock::time_point step_start;
  size_t step = 0;
  StreamingAnalyzer analyzer(
      player_mask,
      [&](const Moves &prefix, const int player, const int trigger_eid) {
        step_start = Clock::now();
        return reviewer(prefix, player, trigger_eid);
      },
      [&](const Analysis_Position &position) {
        const Clock::time_point end = Clock::now();
        ++step;
        markers << std::fixed << std::setprecision(9) << "FULL_ANALYZE_STEP\t" << step
                << "\tEVENT\t" << position.trigger_eid << "\tPLAYER\t" << position.player
                << "\tELAPSED_SECONDS\t"
                << std::chrono::duration<double>(end - analysis_start).count() << "\tSTEP_SECONDS\t"
                << std::chrono::duration<double>(end - step_start).count() << '\n'
                << std::flush;
      },
      [&](const Analysis_Position &position) {
        output << write_position_candidates(position) << '\n' << std::flush;
      },
      [&](const Streaming_Result &result) {
        output << write_position_result(result.position, result.unobserved) << '\n' << std::flush;
      });
  while (std::getline(input, line)) {
    const Moves events = parser.feed_line(line);
    for (const Event &event : events) {
      record.push_back(event);
      analyzer.consume(record);
    }
  }
  parser.finish();
  analyzer.finish();
}
