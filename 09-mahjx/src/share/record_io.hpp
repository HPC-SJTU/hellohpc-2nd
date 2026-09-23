#pragma once

#include <string>
#include <vector>

#include "event.hpp"

Moves parse_game_record(const std::string &text);
Moves load_game_record(const std::string &file_name);
std::string write_game_record(const Moves &events);
void write_game_record_file(const Moves &events, const std::string &file_name);

// Incremental parser for the line-oriented record format. Related data records
// are retained only until their Event is complete.
class RecordParser {
 public:
  RecordParser();
  Moves feed_line(const std::string &line);
  void finish() const;

 private:
  enum Pending { NONE, WIN_RESULT, HANDS };
  Event pending_event_;
  Event last_event_;
  bool have_last_event_;
  int next_eid_;
  bool have_game_;
  bool first_round_;
  bool ended_;
  bool result_complete_;
  Pending pending_;
  int hand_mask_;
  int dealer_;
  int round_wind_;
  int round_;
  int ranking_model_round_;
  int repeat_counter_;
  int deposit_;
  std::array<int, 4> scores_;
};
