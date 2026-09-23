#include "../streaming_analyzer.hpp"

#include <stdexcept>
#include <string>

#include "../analyze.hpp"
#include "test_support.hpp"

namespace {

void expect(const char *name, const bool actual, const bool expected) {
  test_support::expect(name, actual == expected);
}

Event action(const EventType type, const int player, const int tile) {
  Event result;
  result.type = type;
  result.player = player;
  result.tile = tile;
  return result;
}

void expect_non_discard_fields_strict(const EventType type) {
  Event original = action(type, 1, 12);
  original.target = 2;
  original.consumed.push_back(12);
  original.consumed.push_back(12);
  Event changed = original;
  changed.target = 3;
  expect("non-discard target remains strict", analysis_same_action(original, changed), false);
  changed = original;
  changed.from_draw = true;
  expect("non-discard from_draw remains strict", analysis_same_action(original, changed), false);
  changed = original;
  changed.consumed[1] = 13;
  expect("non-discard consumed remains strict", analysis_same_action(original, changed), false);
}

Analysis_Position reviewed(const Moves &, const int player, const int eid) {
  Analysis_Position position;
  position.player = player;
  position.trigger_eid = eid;
  Decision a;
  Event pass;
  pass.type = EventType::PASS;
  pass.player = player;
  a.actions.push_back(pass);
  Decision b;
  Event win;
  win.type = EventType::WIN;
  win.player = player;
  b.actions.push_back(win);
  position.candidates.push_back(a);
  position.candidates.push_back(b);
  return position;
}

Event event(const EventType type, const int player, const int eid) {
  Event value;
  value.type = type;
  value.player = player;
  value.eid = eid;
  return value;
}

std::vector<Streaming_Result> consume(StreamingAnalyzer *analyzer, Moves *record,
                                      const Event &value) {
  record->push_back(value);
  return analyzer->consume(*record);
}

void expect_actual(const char *name, const Streaming_Result &result, const EventType first,
                   const size_t count) {
  expect(name,
         !result.unobserved && result.position.actual.size() == count &&
             result.position.actual[0].type == first,
         true);
}

void test_draw_actuals() {
  const EventType simple[] = {EventType::DISCARD, EventType::CONCEALED_KAN, EventType::UPGRADED_KAN,
                              EventType::WIN};
  for (const EventType type : simple) {
    StreamingAnalyzer analyzer(1, reviewed);
    Moves record;
    consume(&analyzer, &record, event(EventType::DRAW, 0, 1));
    const std::vector<Streaming_Result> out = consume(&analyzer, &record, event(type, 0, 2));
    expect_actual("simple DRAW actual", out[0], type, 1);
  }
  {
    StreamingAnalyzer analyzer(1, reviewed);
    Moves record;
    consume(&analyzer, &record, event(EventType::DRAW, 0, 1));
    consume(&analyzer, &record, event(EventType::RIICHI, 0, 2));
    consume(&analyzer, &record, event(EventType::RIICHI_ACCEPTED, 0, 3));
    const std::vector<Streaming_Result> out =
        consume(&analyzer, &record, event(EventType::DISCARD, 0, 4));
    expect_actual("RIICHI compound", out[0], EventType::RIICHI, 2);
  }
  {
    StreamingAnalyzer analyzer(1, reviewed);
    Moves record;
    consume(&analyzer, &record, event(EventType::DRAW, 0, 1));
    const std::vector<Streaming_Result> out =
        consume(&analyzer, &record, event(EventType::DRAWN_NINE_TERMINALS, 0, 2));
    expect_actual("nine terminals mapping", out[0], EventType::NINE_TERMINALS, 1);
  }
}

void test_response_groups() {
  const EventType boundaries[] = {EventType::DRAW, EventType::DRAWN_EXHAUSTIVE, EventType::ROUND,
                                  EventType::END_GAME};
  for (const EventType boundary : boundaries) {
    StreamingAnalyzer analyzer(15, reviewed);
    Moves record;
    consume(&analyzer, &record, event(EventType::DISCARD, 0, 1));
    Event dora = event(EventType::DORA, -1, 2);
    expect("side effect does not close response", consume(&analyzer, &record, dora).empty(), true);
    const std::vector<Streaming_Result> out = consume(&analyzer, &record, event(boundary, 1, 3));
    expect("PASS group size", out.size() == 3, true);
    for (size_t i = 0; i < out.size(); ++i) {
      expect("PASS player order", out[i].position.player == static_cast<int>(i + 1), true);
      expect_actual("PASS actual", out[i], EventType::PASS, 1);
    }
  }
  {
    StreamingAnalyzer analyzer(15, reviewed);
    Moves record;
    consume(&analyzer, &record, event(EventType::DISCARD, 0, 1));
    const std::vector<Streaming_Result> out =
        consume(&analyzer, &record, event(EventType::OPEN_KAN, 2, 2));
    expect("OPEN_KAN group", out.size() == 3, true);
    expect("OPEN_KAN first unobserved", out[0].unobserved, true);
    expect_actual("OPEN_KAN responder", out[1], EventType::OPEN_KAN, 1);
    expect("OPEN_KAN last unobserved", out[2].unobserved, true);
  }
  {
    StreamingAnalyzer analyzer(15, reviewed);
    Moves record;
    consume(&analyzer, &record, event(EventType::DISCARD, 0, 1));
    consume(&analyzer, &record, event(EventType::PON, 2, 2));
    consume(&analyzer, &record, event(EventType::DORA, -1, 3));
    const std::vector<Streaming_Result> out =
        consume(&analyzer, &record, event(EventType::DISCARD, 2, 4));
    expect("one response result count", out.size() == 3, true);
    expect("one response unobserved", out[0].unobserved && !out[1].unobserved && out[2].unobserved,
           true);
    expect_actual("PON compound", out[1], EventType::PON, 2);
  }
  {
    StreamingAnalyzer analyzer(15, reviewed);
    Moves record;
    consume(&analyzer, &record, event(EventType::DISCARD, 0, 1));
    const std::vector<Streaming_Result> out =
        consume(&analyzer, &record, event(EventType::WIN, 3, 2));
    expect("WIN response group", out.size() == 3 && out[0].unobserved && out[1].unobserved, true);
    expect_actual("WIN responder", out[2], EventType::WIN, 1);
  }
}

void test_upgraded_kan_order_and_protocol_error() {
  std::vector<std::string> order;
  StreamingAnalyzer analyzer(
      15, reviewed, StreamingAnalyzer::PositionObserver(),
      [&](const Analysis_Position &position) {
        order.push_back(std::string("P") + char('0' + position.player));
      },
      [&](const Streaming_Result &result) {
        order.push_back(std::string("T") + char('0' + result.position.player));
      });
  Moves record;
  consume(&analyzer, &record, event(EventType::DRAW, 0, 1));
  order.clear();
  consume(&analyzer, &record, event(EventType::UPGRADED_KAN, 0, 2));
  expect("upgraded closes before trigger candidates",
         order.size() == 4 && order[0] == "T0" && order[1] == "P1" && order[2] == "P2" &&
             order[3] == "P3",
         true);

  StreamingAnalyzer invalid(1, reviewed);
  Moves invalid_record;
  consume(&invalid, &invalid_record, event(EventType::DRAW, 0, 1));
  consume(&invalid, &invalid_record, event(EventType::RIICHI, 0, 2));
  consume(&invalid, &invalid_record, event(EventType::DORA, -1, 3));
  try {
    consume(&invalid, &invalid_record, event(EventType::DISCARD, 1, 4));
  } catch (const std::runtime_error &error) {
    expect("compound protocol error text",
           std::string(error.what()) == "compound action must be followed by same-player DISCARD",
           true);
    return;
  }
  test_support::fail("invalid compound accepted unexpectedly");
}

void test_review_marker_without_candidates() {
  int reviews = 0;
  int candidates = 0;
  StreamingAnalyzer analyzer(
      15,
      [](const Moves &, int player, int eid) {
        Analysis_Position position;
        position.player = player;
        position.trigger_eid = eid;
        return position;
      },
      [&](const Analysis_Position &) { ++reviews; },
      [&](const Analysis_Position &) { ++candidates; });
  Moves record;
  consume(&analyzer, &record, event(EventType::DISCARD, 0, 1));
  expect("all masked response players reviewed", reviews == 3, true);
  expect("no candidates below threshold", candidates == 0, true);
  analyzer.finish();
}

}  // namespace

int main() {
  Event discard = action(EventType::DISCARD, 3, 3);
  Event normalized_discard = discard;
  normalized_discard.from_draw = true;
  expect("discard ignores from_draw", analysis_same_action(discard, normalized_discard), true);

  Event other_tile = normalized_discard;
  other_tile.tile = 4;
  expect("discard tile remains strict", analysis_same_action(discard, other_tile), false);
  Event other_player = normalized_discard;
  other_player.player = 2;
  expect("discard player remains strict", analysis_same_action(discard, other_player), false);
  Event other_type = normalized_discard;
  other_type.type = EventType::DRAW;
  expect("discard type remains strict", analysis_same_action(discard, other_type), false);

  expect_non_discard_fields_strict(EventType::CHII);
  expect_non_discard_fields_strict(EventType::PON);
  expect_non_discard_fields_strict(EventType::OPEN_KAN);
  expect_non_discard_fields_strict(EventType::WIN);

  Moves actual(1, discard);
  Moves candidate(1, normalized_discard);
  expect("move comparison uses discard equivalence", analysis_same_moves(actual, candidate), true);
  candidate.push_back(action(EventType::PON, 1, 12));
  expect("move size remains strict", analysis_same_moves(actual, candidate), false);
  test_draw_actuals();
  test_response_groups();
  test_upgraded_kan_order_and_protocol_error();
  test_review_marker_without_candidates();
  return 0;
}
