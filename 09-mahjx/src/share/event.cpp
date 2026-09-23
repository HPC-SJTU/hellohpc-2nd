#include "event.hpp"

const char *event_type_text(const EventType type) {
  switch (type) {
    case EventType::GAME:
      return "GAME";
    case EventType::ROUND:
      return "ROUND";
    case EventType::DRAW:
      return "DRAW";
    case EventType::DISCARD:
      return "DISCARD";
    case EventType::RIICHI:
      return "RIICHI";
    case EventType::RIICHI_ACCEPTED:
      return "RIICHI_ACCEPTED";
    case EventType::DORA:
      return "DORA";
    case EventType::CHII:
      return "CHII";
    case EventType::PON:
      return "PON";
    case EventType::OPEN_KAN:
      return "OPEN_KAN";
    case EventType::CONCEALED_KAN:
      return "CONCEALED_KAN";
    case EventType::UPGRADED_KAN:
      return "UPGRADED_KAN";
    case EventType::WIN:
      return "WIN";
    case EventType::NINE_TERMINALS:
      return "NINE_TERMINALS";
    case EventType::PASS:
      return "PASS";
    case EventType::DRAWN_EXHAUSTIVE:
      return "DRAWN_EXHAUSTIVE";
    case EventType::DRAWN_NINE_TERMINALS:
      return "DRAWN_NINE_TERMINALS";
    case EventType::END_GAME:
      return "END_GAME";
  }
  return "";
}

bool is_round_result(const EventType type) {
  return type == EventType::WIN || type == EventType::DRAWN_EXHAUSTIVE ||
         type == EventType::DRAWN_NINE_TERMINALS;
}

bool Event::operator==(const Event &o) const {
  return type == o.type && player == o.player && target == o.target && tile == o.tile &&
         from_draw == o.from_draw && consumed == o.consumed;
}
