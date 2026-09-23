#pragma once

#include <array>
#include <vector>

enum class EventType {
  GAME,
  ROUND,
  DRAW,
  DISCARD,
  RIICHI,
  RIICHI_ACCEPTED,
  DORA,
  CHII,
  PON,
  OPEN_KAN,
  CONCEALED_KAN,
  UPGRADED_KAN,
  WIN,
  NINE_TERMINALS,
  PASS,
  DRAWN_EXHAUSTIVE,
  DRAWN_NINE_TERMINALS,
  END_GAME
};

struct Event {
  EventType type = EventType::PASS;
  int eid = -1;
  int player = -1;
  int target = -1;
  int tile = -1;  // -1 is the hidden tile.
  bool from_draw = false;
  int dealer = -1;
  bool red_dora = false;
  int round_wind = -1;
  int round = 0;
  int ranking_model_round = 0;
  int repeat_counter = 0;
  int deposit = 0;
  int dora_indicator = -1;
  int han = 0;
  int fu = 0;
  std::vector<int> consumed;
  std::vector<int> hand;
  std::array<std::vector<int>, 4> hands;
  std::vector<int> wall;
  std::vector<int> underneath_dora_indicators;
  std::array<int, 4> scores = {{0, 0, 0, 0}};
  std::array<bool, 4> ready = {{false, false, false, false}};

  bool operator==(const Event &other) const;
  bool operator!=(const Event &other) const { return !(*this == other); }
};

using Moves = std::vector<Event>;

const char *event_type_text(EventType type);
bool is_round_result(EventType type);
