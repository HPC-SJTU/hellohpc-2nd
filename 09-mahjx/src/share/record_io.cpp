#include "record_io.hpp"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "types.hpp"

namespace {
std::vector<std::string> fields(const std::string &line) {
  if (line.empty() || line.front() == '\t' || line.back() == '\t')
    throw std::runtime_error("invalid empty field");
  std::vector<std::string> out;
  size_t begin = 0;
  for (size_t i = 0; i <= line.size(); ++i) {
    if (i == line.size() || line[i] == '\t') {
      if (i == begin) throw std::runtime_error("invalid empty field");
      out.push_back(line.substr(begin, i - begin));
      begin = i + 1;
    }
  }
  return out;
}

int integer(const std::string &s, bool nonnegative = true) {
  if (s.empty() || s == "-0" || (s.size() > 1 && s[0] == '0') ||
      (s.size() > 2 && s[0] == '-' && s[1] == '0'))
    throw std::runtime_error("invalid integer");
  size_t p = s[0] == '-' ? 1 : 0;
  if (p == s.size()) throw std::runtime_error("invalid integer");
  for (; p < s.size(); ++p)
    if (s[p] < '0' || s[p] > '9') throw std::runtime_error("invalid integer");
  errno = 0;
  char *end = NULL;
  long value = std::strtol(s.c_str(), &end, 10);
  if (errno || *end || value < INT_MIN || value > INT_MAX || (nonnegative && value < 0))
    throw std::runtime_error("integer out of range");
  return static_cast<int>(value);
}

bool boolean(const std::string &s) {
  if (s == "0") return false;
  if (s == "1") return true;
  throw std::runtime_error("invalid boolean");
}

int tile(const std::string &s, bool hidden) {
  if (hidden && s == "?") return -1;
  bool valid = s == "E" || s == "S" || s == "W" || s == "N" || s == "P" || s == "F" || s == "C";
  if (s.size() == 2 && s[0] >= '1' && s[0] <= '9' && (s[1] == 'm' || s[1] == 'p' || s[1] == 's'))
    valid = true;
  if (s.size() == 3 && s[0] == '5' && (s[1] == 'm' || s[1] == 'p' || s[1] == 's') && s[2] == 'r')
    valid = true;
  if (!valid) throw std::runtime_error("invalid tile");
  const int value = parse_tile(s);
  if (!is_valid_tile(value)) throw std::runtime_error("invalid tile");
  return value;
}

void count(const std::vector<std::string> &f, size_t n) {
  if (f.size() != n) throw std::runtime_error("incorrect field count for " + f[0]);
}

void player(int p) {
  if (!is_valid_player(p)) throw std::runtime_error("invalid player");
}

std::string t(int value) { return value < 0 ? "?" : tile_string(value); }
void add(std::ostringstream &out, const std::string &value) { out << '\t' << value; }
void add(std::ostringstream &out, int value) { out << '\t' << value; }

Event action(const std::vector<std::string> &f, int expected_eid) {
  if (f.size() < 3 || integer(f[1]) != expected_eid) throw std::runtime_error("invalid action eid");
  Event e;
  e.eid = expected_eid;
  const std::string &a = f[2];
  if (a == "DRAW") {
    count(f, 5);
    e.type = EventType::DRAW;
    e.player = integer(f[3]);
    player(e.player);
    e.tile = tile(f[4], true);
  } else if (a == "DISCARD") {
    count(f, 6);
    e.type = EventType::DISCARD;
    e.player = integer(f[3]);
    player(e.player);
    e.tile = tile(f[4], false);
    e.from_draw = boolean(f[5]);
  } else if (a == "RIICHI" || a == "RIICHI_ACCEPTED") {
    count(f, 4);
    e.type = a == "RIICHI" ? EventType::RIICHI : EventType::RIICHI_ACCEPTED;
    e.player = integer(f[3]);
    player(e.player);
  } else if (a == "DORA") {
    count(f, 4);
    e.type = EventType::DORA;
    e.dora_indicator = tile(f[3], false);
  } else if (a == "CHII" || a == "PON") {
    count(f, 8);
    e.type = a == "CHII" ? EventType::CHII : EventType::PON;
    e.player = integer(f[3]);
    e.target = integer(f[4]);
    player(e.player);
    player(e.target);
    e.tile = tile(f[5], false);
    e.consumed = {tile(f[6], false), tile(f[7], false)};
  } else if (a == "OPEN_KAN") {
    count(f, 9);
    e.type = EventType::OPEN_KAN;
    e.player = integer(f[3]);
    e.target = integer(f[4]);
    player(e.player);
    player(e.target);
    e.tile = tile(f[5], false);
    for (int i = 6; i < 9; ++i) e.consumed.push_back(tile(f[i], false));
  } else if (a == "CONCEALED_KAN") {
    count(f, 8);
    e.type = EventType::CONCEALED_KAN;
    e.player = integer(f[3]);
    player(e.player);
    for (int i = 4; i < 8; ++i) e.consumed.push_back(tile(f[i], false));
  } else if (a == "UPGRADED_KAN") {
    count(f, 8);
    e.type = EventType::UPGRADED_KAN;
    e.player = integer(f[3]);
    player(e.player);
    e.tile = tile(f[4], false);
    for (int i = 5; i < 8; ++i) e.consumed.push_back(tile(f[i], false));
  } else if (a == "WIN") {
    count(f, 6);
    e.type = EventType::WIN;
    e.player = integer(f[3]);
    e.target = integer(f[4]);
    player(e.player);
    player(e.target);
    e.tile = tile(f[5], false);
  } else
    throw std::runtime_error("unknown game action");
  return e;
}
}  // namespace

RecordParser::RecordParser()
    : have_last_event_(false),
      next_eid_(0),
      have_game_(false),
      first_round_(true),
      ended_(false),
      result_complete_(false),
      pending_(NONE),
      hand_mask_(0),
      dealer_(0),
      round_wind_(0),
      round_(1),
      ranking_model_round_(0),
      repeat_counter_(0),
      deposit_(0),
      scores_({{0, 0, 0, 0}}) {}

Moves RecordParser::feed_line(const std::string &line) {
  if (!line.empty() && line.back() == '\r') throw std::runtime_error("CR line ending is invalid");
  std::vector<std::string> f = fields(line);
  if (ended_) throw std::runtime_error("record after END_GAME");
  if (have_game_ && first_round_ && f[0] != "ROUND")
    throw std::runtime_error("ROUND must follow GAME");
  if (result_complete_) {
    if (last_event_.type == EventType::WIN && f[0] == "ACTION" && f.size() >= 3 && f[2] == "WIN")
      throw std::runtime_error("multiple WIN results in one round are not supported");
    if (f[0] != "ROUND" && f[0] != "END_GAME")
      throw std::runtime_error("result group must be followed by ROUND or END_GAME");
  }
  if (pending_ == WIN_RESULT && f[0] != "WIN_RESULT")
    throw std::runtime_error("WIN_RESULT must immediately follow WIN");
  if (pending_ == HANDS && f[0] != "HAND")
    throw std::runtime_error("four HAND records must immediately follow their owner");
  Moves emitted;
  if (f[0] == "GAME") {
    if (have_game_) throw std::runtime_error("duplicate GAME");
    count(f, 13);
    if (integer(f[1]) != 0) throw std::runtime_error("GAME eid must be zero");
    Event game;
    game.type = EventType::GAME;
    game.eid = 0;
    game.dealer = integer(f[2]);
    player(game.dealer);
    game.red_dora = boolean(f[3]);
    game.round_wind = parse_wind(f[4]);
    if (game.round_wind < 0) throw std::runtime_error("invalid wind");
    game.round = integer(f[5]);
    game.ranking_model_round = integer(f[6]);
    game.repeat_counter = integer(f[7]);
    game.deposit = integer(f[8]);
    if (game.round < 1 || game.round > 4) throw std::runtime_error("invalid round");
    if (game.ranking_model_round >= 12) throw std::runtime_error("invalid ranking-model round");
    for (int i = 0; i < 4; ++i) game.scores[i] = integer(f[9 + i], false);
    emitted.push_back(game);
    last_event_ = game;
    have_last_event_ = true;
    have_game_ = true;
    next_eid_ = 1;
    dealer_ = game.dealer;
    round_wind_ = game.round_wind;
    round_ = game.round;
    ranking_model_round_ = game.ranking_model_round;
    repeat_counter_ = game.repeat_counter;
    deposit_ = game.deposit;
    scores_ = game.scores;
  } else if (!have_game_)
    throw std::runtime_error("GAME must be first");
  else if (f[0] == "ROUND") {
    count(f, 3);
    if (integer(f[1]) != next_eid_++) throw std::runtime_error("invalid ROUND eid");
    if (!first_round_) {
      if (!have_last_event_ || !is_round_result(last_event_.type))
        throw std::runtime_error("ROUND must follow result");
      Event current;
      current.type = EventType::ROUND;
      current.dealer = dealer_;
      current.round_wind = round_wind_;
      current.round = round_;
      current.ranking_model_round = ranking_model_round_;
      current.repeat_counter = repeat_counter_;
      current.deposit = deposit_;
      current.scores = scores_;
      Moves transition;
      transition.push_back(current);
      transition.push_back(last_event_);
      const Event &r = last_event_;
      scores_ = r.scores;
      const int next_dealer = cal_next_dealer(transition);
      repeat_counter_ = cal_next_repeat_counter(transition);
      if (r.type == EventType::WIN) deposit_ = 0;
      if (next_dealer != dealer_) {
        ++ranking_model_round_;
        if (++round_ == 5) {
          round_ = 1;
          ++round_wind_;
        }
      }
      dealer_ = next_dealer;
    }
    Event e;
    e.type = EventType::ROUND;
    e.eid = next_eid_ - 1;
    e.dealer = dealer_;
    e.round_wind = round_wind_;
    e.round = round_;
    e.ranking_model_round = ranking_model_round_;
    e.repeat_counter = repeat_counter_;
    e.deposit = deposit_;
    e.scores = scores_;
    e.dora_indicator = tile(f[2], false);
    pending_event_ = e;
    pending_ = HANDS;
    hand_mask_ = 0;
    first_round_ = false;
    result_complete_ = false;
  } else if (f[0] == "ACTION") {
    Event e = action(f, next_eid_++);
    if (e.type == EventType::RIICHI_ACCEPTED) ++deposit_;
    if (e.type == EventType::WIN) {
      pending_event_ = e;
      pending_ = WIN_RESULT;
    } else {
      emitted.push_back(e);
      last_event_ = e;
      have_last_event_ = true;
    }
    hand_mask_ = 0;
    result_complete_ = false;
  } else if (f[0] == "WIN_RESULT") {
    if (pending_ != WIN_RESULT) throw std::runtime_error("orphan WIN_RESULT");
    Event &owner = pending_event_;
    if (f.size() < 10 || integer(f[1]) != owner.eid) throw std::runtime_error("invalid WIN_RESULT");
    owner.han = integer(f[2]);
    owner.fu = integer(f[3]);
    for (int i = 0; i < 4; ++i) owner.scores[i] = integer(f[4 + i], false);
    int hc = integer(f[8]);
    size_t ura_pos = 9 + hc;
    if (ura_pos >= f.size()) throw std::runtime_error("invalid hand count");
    for (int i = 0; i < hc; ++i) owner.hand.push_back(tile(f[9 + i], false));
    int uc = integer(f[ura_pos]);
    if (f.size() != ura_pos + 1 + uc) throw std::runtime_error("invalid ura count");
    for (int i = 0; i < uc; ++i)
      owner.underneath_dora_indicators.push_back(tile(f[ura_pos + 1 + i], false));
    emitted.push_back(owner);
    last_event_ = owner;
    have_last_event_ = true;
    pending_ = NONE;
    result_complete_ = true;
  } else if (f[0] == "DRAWN_EXHAUSTIVE") {
    count(f, 10);
    if (integer(f[1]) != next_eid_++) throw std::runtime_error("invalid eid");
    Event e;
    e.type = EventType::DRAWN_EXHAUSTIVE;
    e.eid = next_eid_ - 1;
    for (int i = 0; i < 4; ++i) e.ready[i] = boolean(f[2 + i]);
    for (int i = 0; i < 4; ++i) e.scores[i] = integer(f[6 + i], false);
    pending_event_ = e;
    pending_ = HANDS;
    hand_mask_ = 0;
  } else if (f[0] == "DRAWN_NINE_TERMINALS") {
    count(f, 7);
    if (integer(f[1]) != next_eid_++) throw std::runtime_error("invalid eid");
    Event e;
    e.type = EventType::DRAWN_NINE_TERMINALS;
    e.eid = next_eid_ - 1;
    e.player = integer(f[2]);
    player(e.player);
    for (int i = 0; i < 4; ++i) e.scores[i] = integer(f[3 + i], false);
    pending_event_ = e;
    pending_ = HANDS;
    hand_mask_ = 0;
  } else if (f[0] == "HAND") {
    if (pending_ != HANDS) throw std::runtime_error("orphan HAND");
    Event &owner = pending_event_;
    if (f.size() < 4 || integer(f[1]) != owner.eid) throw std::runtime_error("invalid HAND");
    int p = integer(f[2]);
    player(p);
    if (hand_mask_ & (1 << p)) throw std::runtime_error("duplicate HAND");
    int n = integer(f[3]);
    if (owner.type == EventType::ROUND && (n != 13 || hand_mask_ != (1 << p) - 1))
      throw std::runtime_error("ROUND requires thirteen tiles for each player in order");
    if (f.size() != size_t(4 + n)) throw std::runtime_error("invalid HAND count");
    for (int i = 0; i < n; ++i) owner.hands[p].push_back(tile(f[4 + i], true));
    if (owner.type == EventType::DRAWN_NINE_TERMINALS) {
      for (const int value : owner.hands[p]) {
        if ((p == owner.player && value < 0) || (p != owner.player && value >= 0))
          throw std::runtime_error("invalid nine-terminals HAND visibility");
      }
    }
    hand_mask_ |= 1 << p;
    if (hand_mask_ == 15) {
      emitted.push_back(owner);
      last_event_ = owner;
      have_last_event_ = true;
      pending_ = NONE;
      result_complete_ = is_round_result(owner.type);
    }
  } else if (f[0] == "END_GAME") {
    count(f, 2);
    if (!have_last_event_ || !is_round_result(last_event_.type))
      throw std::runtime_error("END_GAME must follow result");
    if (integer(f[1]) != next_eid_++) throw std::runtime_error("invalid eid");
    Event e;
    e.type = EventType::END_GAME;
    e.eid = next_eid_ - 1;
    emitted.push_back(e);
    last_event_ = e;
    have_last_event_ = true;
    ended_ = true;
  } else
    throw std::runtime_error("unknown record name");
  return emitted;
}

void RecordParser::finish() const {
  if (!have_game_) throw std::runtime_error("empty game record");
  if (pending_ != NONE) throw std::runtime_error("incomplete related data records");
  if (!have_last_event_ ||
      (!is_round_result(last_event_.type) && last_event_.type != EventType::END_GAME))
    throw std::runtime_error("game record must end after a round result");
}

Moves parse_game_record(const std::string &text) {
  if (text.empty()) throw std::runtime_error("empty game record");
  RecordParser parser;
  Moves result;
  std::istringstream input(text);
  std::string line;
  while (std::getline(input, line)) {
    const Moves emitted = parser.feed_line(line);
    result.insert(result.end(), emitted.begin(), emitted.end());
  }
  if (!input.eof()) throw std::runtime_error("incomplete game record");
  parser.finish();
  return result;
}

Moves load_game_record(const std::string &file_name) {
  std::ifstream in(file_name.c_str());
  if (!in) throw std::runtime_error("cannot open game record");
  std::ostringstream s;
  s << in.rdbuf();
  return parse_game_record(s.str());
}

std::string write_game_record(const Moves &events) {
  std::ostringstream out;
  int eid = 0;
  for (size_t k = 0; k < events.size(); ++k) {
    const Event &e = events[k];
    if (k) out << '\n';
    if (e.type == EventType::GAME) {
      out << "GAME\t" << eid++;
      add(out, e.dealer);
      add(out, e.red_dora ? 1 : 0);
      add(out, wind_string(e.round_wind));
      add(out, e.round);
      add(out, e.ranking_model_round);
      add(out, e.repeat_counter);
      add(out, e.deposit);
      for (int x : e.scores) add(out, x);
    } else if (e.type == EventType::ROUND) {
      int id = eid++;
      out << "ROUND\t" << id;
      add(out, t(e.dora_indicator));
      for (int p = 0; p < 4; ++p) {
        out << '\n' << "HAND\t" << id << '\t' << p << "\t13";
        for (int x : e.hands[p]) add(out, t(x));
      }
    } else if (e.type == EventType::DRAWN_EXHAUSTIVE) {
      int id = eid++;
      out << "DRAWN_EXHAUSTIVE\t" << id;
      for (bool x : e.ready) add(out, x ? 1 : 0);
      for (int x : e.scores) add(out, x);
      for (int p = 0; p < 4; ++p) {
        out << '\n' << "HAND\t" << id << '\t' << p << '\t' << e.hands[p].size();
        for (int x : e.hands[p]) add(out, t(x));
      }
    } else if (e.type == EventType::DRAWN_NINE_TERMINALS) {
      int id = eid++;
      out << "DRAWN_NINE_TERMINALS\t" << id;
      add(out, e.player);
      for (int x : e.scores) add(out, x);
      for (int p = 0; p < 4; ++p) {
        out << '\n' << "HAND\t" << id << '\t' << p << '\t' << e.hands[p].size();
        for (int x : e.hands[p]) add(out, t(x));
      }
    } else if (e.type == EventType::END_GAME) {
      out << "END_GAME\t" << eid++;
    } else {
      int id = eid++;
      out << "ACTION\t" << id << '\t';
      switch (e.type) {
        case EventType::DRAW:
          out << "DRAW";
          add(out, e.player);
          add(out, t(e.tile));
          break;
        case EventType::DISCARD:
          out << "DISCARD";
          add(out, e.player);
          add(out, t(e.tile));
          add(out, e.from_draw ? 1 : 0);
          break;
        case EventType::RIICHI:
          out << "RIICHI";
          add(out, e.player);
          break;
        case EventType::RIICHI_ACCEPTED:
          out << "RIICHI_ACCEPTED";
          add(out, e.player);
          break;
        case EventType::DORA:
          out << "DORA";
          add(out, t(e.dora_indicator));
          break;
        case EventType::CHII:
        case EventType::PON:
        case EventType::OPEN_KAN:
          out << (e.type == EventType::CHII  ? "CHII"
                  : e.type == EventType::PON ? "PON"
                                             : "OPEN_KAN");
          add(out, e.player);
          add(out, e.target);
          add(out, t(e.tile));
          for (int x : e.consumed) add(out, t(x));
          break;
        case EventType::CONCEALED_KAN:
          out << "CONCEALED_KAN";
          add(out, e.player);
          for (int x : e.consumed) add(out, t(x));
          break;
        case EventType::UPGRADED_KAN:
          out << "UPGRADED_KAN";
          add(out, e.player);
          add(out, t(e.tile));
          for (int x : e.consumed) add(out, t(x));
          break;
        case EventType::WIN:
          out << "WIN";
          add(out, e.player);
          add(out, e.target);
          add(out, t(e.tile));
          out << '\n' << "WIN_RESULT\t" << id;
          add(out, e.han);
          add(out, e.fu);
          for (int x : e.scores) add(out, x);
          add(out, (int)e.hand.size());
          for (int x : e.hand) add(out, t(x));
          add(out, (int)e.underneath_dora_indicators.size());
          for (int x : e.underneath_dora_indicators) add(out, t(x));
          break;
        default:
          throw std::runtime_error("candidate-only event in game record");
      }
    }
  }
  return out.str();
}
void write_game_record_file(const Moves &events, const std::string &file_name) {
  std::ofstream out(file_name.c_str());
  if (!out) throw std::runtime_error("cannot write game record");
  out << write_game_record(events);
  if (!events.empty()) out << '\n';
}
