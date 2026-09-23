#include "../share/record_io.hpp"

#include <sstream>
#include <stdexcept>
#include <string>

#include "../share/types.hpp"
#include "test_support.hpp"

namespace {

void expect(const char *name, const bool condition) { test_support::expect(name, condition); }

std::string round_record() {
  std::ostringstream record;
  record << "ROUND\t1\t1m";
  for (int player = 0; player < 4; ++player) {
    record << "\nHAND\t1\t" << player << "\t13";
    for (int i = 0; i < 13; ++i) record << "\t1m";
  }
  return record.str();
}

std::string single_win_record() {
  std::ostringstream record;
  record << "GAME\t0\t0\t1\tE\t1\t0\t0\t0\t25000\t25000\t25000\t25000\n" << round_record();
  record << "\nACTION\t2\tWIN\t1\t2\t1m\n"
         << "WIN_RESULT\t2\t1\t30\t25000\t26500\t23500\t25000\t0\t0";
  return record.str();
}

void test_single_win_parser() {
  const Moves parsed = parse_game_record(single_win_record());
  expect("single WIN accepted", parsed.size() == 3 && parsed.back().type == EventType::WIN);

  const std::string multiple_win = single_win_record() +
                                   "\nACTION\t3\tWIN\t3\t2\t1m\n"
                                   "WIN_RESULT\t3\t1\t30\t25000\t26500\t22000\t26500\t0\t0";
  try {
    parse_game_record(multiple_win);
  } catch (const std::runtime_error &error) {
    expect("consecutive WIN error",
           std::string(error.what()) == "multiple WIN results in one round are not supported");
    return;
  }
  test_support::fail("consecutive WIN accepted unexpectedly");
}

void test_riichi_accepted_record() {
  const std::string text = "GAME\t0\t0\t1\tE\t1\t0\t0\t0\t25000\t25000\t25000\t25000\n";
  std::ostringstream record;
  record << text << round_record();
  record << "\nACTION\t2\tRIICHI_ACCEPTED\t1\n"
         << "DRAWN_NINE_TERMINALS\t3\t1\t25000\t25000\t25000\t25000";
  for (int player = 0; player < 4; ++player) record << "\nHAND\t3\t" << player << "\t0";

  const Moves parsed = parse_game_record(record.str());
  expect("RIICHI_ACCEPTED parsed",
         parsed.size() == 4 && parsed[2].type == EventType::RIICHI_ACCEPTED);
  expect("RIICHI_ACCEPTED serialized",
         write_game_record(parsed).find("ACTION\t2\tRIICHI_ACCEPTED\t1") != std::string::npos);
}

void test_incremental_parser() {
  const std::string text = single_win_record();
  RecordParser parser;
  Moves streamed;
  std::istringstream input(text);
  std::string line;
  int line_number = 0;
  while (std::getline(input, line)) {
    const Moves emitted = parser.feed_line(line);
    ++line_number;
    if (line_number >= 2 && line_number < 6) expect("ROUND waits for HAND", emitted.empty());
    if (line_number == 6)
      expect("ROUND emitted with HAND", emitted.size() == 1 && emitted[0].type == EventType::ROUND);
    if (line_number == 7) expect("WIN waits for WIN_RESULT", emitted.empty());
    streamed.insert(streamed.end(), emitted.begin(), emitted.end());
  }
  parser.finish();
  expect("incremental matches whole parser", streamed == parse_game_record(text));

  RecordParser truncated_round;
  truncated_round.feed_line("GAME\t0\t0\t1\tE\t1\t0\t0\t0\t25000\t25000\t25000\t25000");
  truncated_round.feed_line("ROUND\t1\t1m");
  try {
    truncated_round.finish();
    test_support::fail("truncated related record accepted unexpectedly");
  } catch (const std::runtime_error &error) {
    expect("truncated related record error",
           std::string(error.what()) == "incomplete related data records");
  }

  RecordParser truncated_action;
  truncated_action.feed_line("GAME\t0\t0\t1\tE\t1\t0\t0\t0\t25000\t25000\t25000\t25000");
  std::istringstream start(round_record());
  while (std::getline(start, line)) truncated_action.feed_line(line);
  truncated_action.feed_line("ACTION\t2\tDRAW\t0\t1m");
  try {
    truncated_action.finish();
    test_support::fail("mid-round EOF accepted unexpectedly");
  } catch (const std::runtime_error &error) {
    expect("mid-round EOF error",
           std::string(error.what()) == "game record must end after a round result");
  }

  RecordParser drawn;
  drawn.feed_line("GAME\t0\t0\t1\tE\t1\t0\t0\t0\t25000\t25000\t25000\t25000");
  std::istringstream drawn_start(round_record());
  while (std::getline(drawn_start, line)) drawn.feed_line(line);
  expect("drawn waits for HAND records",
         drawn.feed_line("DRAWN_EXHAUSTIVE\t2\t0\t0\t0\t0\t25000\t25000\t25000\t25000").empty());
  for (int player = 0; player < 3; ++player)
    expect("partial HAND does not emit",
           drawn.feed_line("HAND\t2\t" + std::to_string(player) + "\t0").empty());
  const Moves drawn_event = drawn.feed_line("HAND\t2\t3\t0");
  expect("four HAND records emit result",
         drawn_event.size() == 1 && drawn_event[0].type == EventType::DRAWN_EXHAUSTIVE);
  drawn.finish();

  RecordParser consecutive;
  std::istringstream complete(single_win_record());
  while (std::getline(complete, line)) consecutive.feed_line(line);
  try {
    consecutive.feed_line("ACTION\t3\tWIN\t3\t2\t1m");
  } catch (const std::runtime_error &error) {
    expect("stream consecutive WIN error",
           std::string(error.what()) == "multiple WIN results in one round are not supported");
    return;
  }
  test_support::fail("stream consecutive WIN accepted unexpectedly");
}

void test_unknown_starting_hands() {
  std::string text = single_win_record();
  for (int player = 1; player < 4; ++player) {
    const std::string prefix = "HAND\t1\t" + std::to_string(player) + "\t13";
    const size_t begin = text.find(prefix) + prefix.size();
    const size_t end = text.find('\n', begin);
    std::string hidden;
    for (int i = 0; i < 13; ++i) hidden += "\t?";
    text.replace(begin, end - begin, hidden);
  }
  const Moves parsed = parse_game_record(text);
  expect("known initial hand", parsed[1].hands[0].size() == 13 && parsed[1].hands[0][0] == 1);
  for (int player = 1; player < 4; ++player) {
    expect("hidden initial hand size", parsed[1].hands[player].size() == 13);
    for (int value : parsed[1].hands[player]) expect("hidden tile preserved", value == -1);
  }
  const Moves restored = parse_game_record(write_game_record(parsed));
  expect("initial hands round trip", restored[1].hands == parsed[1].hands);
  expect("initial dora round trip", restored[1].dora_indicator == parsed[1].dora_indicator);
  const Game_State state = get_game_state_start_round(parsed[1]);
  expect("known hand materialized", state.player_state[0].hand[1] == 13);
  for (int player = 1; player < 4; ++player)
    for (int tile = 0; tile < 38; ++tile)
      expect("unknown hand has no invented tiles", state.player_state[player].hand[tile] == 0);
}

}  // namespace

int main(int argc, char **argv) {
  test_unknown_starting_hands();
  test_single_win_parser();
  test_riichi_accepted_record();
  test_incremental_parser();
  for (int arg = 1; arg < argc; ++arg) {
    const Moves original = load_game_record(argv[arg]);
    const Moves restored = parse_game_record(write_game_record(original));
    expect("record round trip", original == restored);
    for (size_t i = 0; i < original.size(); ++i) {
      expect("hands round trip", original[i].hands == restored[i].hands);
      expect("dora round trip", original[i].dora_indicator == restored[i].dora_indicator);
      expect("scores round trip", original[i].scores == restored[i].scores);
      expect("dealer round trip", original[i].dealer == restored[i].dealer);
    }
  }
  return 0;
}
