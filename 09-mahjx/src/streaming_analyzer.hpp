#pragma once

#include <functional>
#include <iosfwd>
#include <vector>

#include "analyze.hpp"

struct Streaming_Result {
  Streaming_Result(const Analysis_Position &position_in, bool unobserved_in)
      : position(position_in), unobserved(unobserved_in) {}
  Analysis_Position position;
  bool unobserved;
};

class StreamingAnalyzer {
 public:
  typedef std::function<Analysis_Position(const Moves &, int, int)> Reviewer;
  typedef std::function<void(const Analysis_Position &)> PositionObserver;
  typedef std::function<void(const Streaming_Result &)> ResultObserver;

  StreamingAnalyzer(int player_mask, const Reviewer &reviewer,
                    const PositionObserver &review_observer = PositionObserver(),
                    const PositionObserver &candidate_observer = PositionObserver(),
                    const ResultObserver &result_observer = ResultObserver());
  std::vector<Streaming_Result> consume(const Moves &record);
  void finish() const;

 private:
  struct PendingGroup {
    Event trigger;
    std::vector<Analysis_Position> positions;
    bool waiting_discard = false;
    int compound_player = -1;
    Event compound_action;
  };

  int player_mask_;
  Reviewer reviewer_;
  PositionObserver review_observer_;
  PositionObserver candidate_observer_;
  ResultObserver result_observer_;
  std::vector<PendingGroup> groups_;

  std::vector<Streaming_Result> advance(const Event &event);
  void create_group(const Moves &record, const Event &trigger);
};

void run_full_analyze_protocol(std::istream &input, std::ostream &output, std::ostream &markers,
                               int player_mask, const StreamingAnalyzer::Reviewer &reviewer);
