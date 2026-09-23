#include "match_config.hpp"

#include <cmath>

namespace {

std::vector<std::string> split_record(const std::string &line) {
  assert_with_out(!line.empty(), "empty configuration record");
  assert_with_out(line.find('\r') == std::string::npos, "CR line ending is invalid");
  assert_with_out(line.front() != '\t' && line.back() != '\t',
                  "invalid tab at configuration record boundary");
  const std::vector<std::string> fields = str_split(line, '\t');
  for (const std::string &field : fields) {
    assert_with_out(!field.empty(), "empty configuration field");
  }
  return fields;
}

int parse_integer(const std::string &text) {
  assert_with_out(!text.empty(), "empty integer");
  assert_with_out(text != "-0", "negative zero is invalid");
  size_t pos = 0;
  if (text[0] == '-') {
    assert_with_out(text.size() > 1, "invalid integer");
    pos = 1;
  }
  assert_with_out(text[pos] != '0' || text.size() == pos + 1, "integer has a leading zero");
  for (; pos < text.size(); pos++) {
    assert_with_out('0' <= text[pos] && text[pos] <= '9', "invalid integer");
  }
  return std::stoi(text);
}

double parse_float(const std::string &text) {
  assert_with_out(!text.empty(), "empty float");
  size_t position = text[0] == '-' ? 1 : 0;
  assert_with_out(position < text.size(), "invalid float");
  if (text[position] == '0') {
    position++;
    assert_with_out(position == text.size() || text[position] < '0' || text[position] > '9',
                    "float has a leading zero");
  } else {
    assert_with_out('1' <= text[position] && text[position] <= '9', "invalid float");
    while (position < text.size() && '0' <= text[position] && text[position] <= '9') position++;
  }
  if (position < text.size() && text[position] == '.') {
    position++;
    const size_t fraction_begin = position;
    while (position < text.size() && '0' <= text[position] && text[position] <= '9') position++;
    assert_with_out(position > fraction_begin, "empty float fraction");
  }
  if (position < text.size() && (text[position] == 'e' || text[position] == 'E')) {
    position++;
    if (position < text.size() && (text[position] == '+' || text[position] == '-')) position++;
    assert_with_out(position < text.size(), "empty float exponent");
    if (text[position] == '0') {
      position++;
      assert_with_out(position == text.size(), "float exponent has a leading zero");
    } else {
      assert_with_out('1' <= text[position] && text[position] <= '9', "invalid float exponent");
      while (position < text.size() && '0' <= text[position] && text[position] <= '9') position++;
    }
  }
  assert_with_out(position == text.size(), "invalid float");
  size_t consumed = 0;
  const double value = std::stod(text, &consumed);
  assert_with_out(consumed == text.size() && std::isfinite(value), "invalid float");
  return value;
}

bool parse_boolean(const std::string &text) {
  assert_with_out(text == "0" || text == "1", "invalid boolean");
  return text == "1";
}

void parse_game(const std::vector<std::string> &fields, Match_Config &config) {
  assert_with_out(fields.size() == 13, "invalid GAME field count");
  assert_with_out(parse_integer(fields[1]) == 0, "GAME eid must be zero");
  Game_Origin game;
  game.dealer = parse_integer(fields[2]);
  game.red_dora = parse_boolean(fields[3]);
  game.round_wind = parse_wind(fields[4]);
  game.round = parse_integer(fields[5]);
  game.ranking_model_round = parse_integer(fields[6]);
  game.repeat_counter = parse_integer(fields[7]);
  game.deposit = parse_integer(fields[8]);
  for (int player = 0; player < 4; player++) {
    game.scores[player] = parse_integer(fields[9 + player]);
  }
  assert_with_out(is_valid_player(game.dealer), "invalid GAME dealer");
  assert_with_out(0 <= game.round_wind && game.round_wind < 4, "invalid GAME round wind");
  assert_with_out(1 <= game.round && game.round <= 4, "invalid GAME round");
  assert_with_out(0 <= game.ranking_model_round && game.ranking_model_round < 12,
                  "invalid GAME ranking-model round");
  assert_with_out(0 <= game.repeat_counter && 0 <= game.deposit, "invalid GAME counter");
  for (const Game_Origin &existing : config.games) {
    assert_with_out(existing.dealer != game.dealer, "duplicate GAME dealer");
  }
  config.games.push_back(game);
}

void parse_hanfu(const std::vector<std::string> &fields,
                 std::array<std::array<double, 12>, 14> &weights) {
  const int value_count = parse_integer(fields[3]);
  assert_with_out(value_count == static_cast<int>(fields.size()) - 4, "invalid han-fu value count");
  const int triple_count = parse_integer(fields[4]);
  assert_with_out(value_count == 1 + 3 * triple_count, "invalid han-fu triple count");
  weights = {};
  std::array<std::array<bool, 12>, 14> seen = {};
  double sum = 0;
  for (int i = 0; i < triple_count; i++) {
    const int han = parse_integer(fields[5 + 3 * i]);
    const int fu_index = parse_integer(fields[6 + 3 * i]);
    const double weight = parse_float(fields[7 + 3 * i]);
    assert_with_out(1 <= han && han <= 13, "invalid han");
    assert_with_out(1 <= fu_index && fu_index <= 11, "invalid fu index");
    assert_with_out(!seen[han][fu_index], "duplicate han-fu pair");
    assert_with_out(0 <= weight && weight <= 1, "invalid han-fu weight");
    seen[han][fu_index] = true;
    weights[han][fu_index] = weight;
    sum += weight;
  }
  assert_with_out(std::fabs(sum - 1) < 1e-6, "invalid han-fu weight sum");
}

void parse_tactic(const std::vector<std::string> &fields, Match_Config &config,
                  std::array<std::set<std::string>, 4> &keys) {
  assert_with_out(fields.size() >= 5, "invalid TACTIC field count");
  const int player = parse_integer(fields[1]);
  assert_with_out(is_valid_player(player), "invalid TACTIC player");
  const std::string &key = fields[2];
  assert_with_out(keys[player].insert(key).second, "duplicate TACTIC key");
  const int value_count = parse_integer(fields[3]);
  assert_with_out(value_count == static_cast<int>(fields.size()) - 4, "invalid TACTIC value count");
  Tactics &tactics = config.tactics[player];
  const auto integer = [&](const int index) { return parse_integer(fields[4 + index]); };
  const auto boolean = [&](const int index) { return parse_boolean(fields[4 + index]); };
  const auto scalar = [&]() { assert_with_out(value_count == 1, "invalid scalar TACTIC count"); };
  if (key == "turn_pt") {
    assert_with_out(value_count == 4, "invalid turn_pt count");
    for (int i = 0; i < 4; i++) tactics.turn_pt[i] = integer(i);
  } else if (key == "hand_change_count") {
    assert_with_out(value_count == 7, "invalid hand_change_count count");
    for (int i = 0; i < 7; i++) tactics.hand_change_count[i] = integer(i);
  } else if (key == "consider_kan") {
    scalar();
    tactics.consider_kan = boolean(0);
  } else if (key == "do_nine_terminals") {
    scalar();
    tactics.do_nine_terminals = boolean(0);
  } else if (key == "fold_exp_at_dp_open_meld") {
    scalar();
    tactics.fold_exp_at_dp_open_meld = boolean(0);
  } else if (key == "inclusive_shanten_num_always") {
    scalar();
    tactics.inclusive_shanten_num_always = integer(0);
  } else if (key == "inclusive_shanten_num_other_riichi") {
    scalar();
    tactics.inclusive_shanten_num_other_riichi = integer(0);
  } else if (key == "inclusive_shanten_num_open_meld") {
    scalar();
    tactics.inclusive_shanten_num_open_meld = integer(0);
  } else if (key == "inclusive_shanten_num_open_meld_other_riichi") {
    scalar();
    tactics.inclusive_shanten_num_open_meld_other_riichi = integer(0);
  } else if (key == "max_open_meld_num") {
    scalar();
    tactics.max_open_meld_num = integer(0);
  } else if (key == "han_shift_prob_kan") {
    assert_with_out(value_count == 14, "invalid han_shift_prob_kan count");
    double sum = 0;
    for (int i = 0; i < 14; i++) {
      tactics.han_shift_prob_kan[i] = parse_float(fields[4 + i]);
      assert_with_out(0 <= tactics.han_shift_prob_kan[i] && tactics.han_shift_prob_kan[i] <= 1,
                      "invalid han_shift_prob_kan value");
      sum += tactics.han_shift_prob_kan[i];
    }
    assert_with_out(std::fabs(sum - 1) < 1e-6, "invalid han_shift_prob_kan sum");
  } else if (key == "hanfu_weight_tsumo") {
    parse_hanfu(fields, tactics.hanfu_weight_tsumo);
  } else if (key == "hanfu_weight_ron") {
    parse_hanfu(fields, tactics.hanfu_weight_ron);
  } else if (key == "win_coeff_tp_fnm") {
    scalar();
    tactics.win_coeff_tp_fnm = integer(0);
  } else if (key == "win_coeff_tp_an") {
    scalar();
    tactics.win_coeff_tp_an = integer(0);
  } else if (key == "tenpai_after_est_begin") {
    scalar();
    tactics.tenpai_after_est_begin = integer(0);
  } else if (key == "other_end_ar") {
    scalar();
    tactics.other_end_ar = parse_boolean(fields[4]);
  } else {
    assert_with_out(false, "unknown TACTIC key");
  }
}

}  // namespace

Match_Config load_match_config(const std::string &file_name) {
  std::ifstream input(file_name);
  assert_with_out(input.is_open(), "cannot open match configuration");
  Match_Config config;
  std::array<std::set<std::string>, 4> tactic_keys;
  bool has_result_dir = false;
  bool has_tactic = false;
  std::string line;
  while (std::getline(input, line)) {
    const std::vector<std::string> fields = split_record(line);
    if (fields[0] == "RESULT_DIR") {
      assert_with_out(!has_result_dir && config.games.empty() && !has_tactic,
                      "invalid RESULT_DIR position");
      assert_with_out(fields.size() == 2, "invalid RESULT_DIR field count");
      config.result_dir = fields[1];
      has_result_dir = true;
    } else if (fields[0] == "GAME") {
      assert_with_out(has_result_dir && !has_tactic, "invalid GAME position");
      parse_game(fields, config);
    } else if (fields[0] == "TACTIC") {
      assert_with_out(has_result_dir && !config.games.empty(), "invalid TACTIC position");
      has_tactic = true;
      parse_tactic(fields, config, tactic_keys);
    } else {
      assert_with_out(false, "unknown configuration record");
    }
  }
  assert_with_out(has_result_dir && !config.games.empty(), "incomplete match configuration");
  for (int player = 0; player < 4; player++) {
    assert_with_out(tactic_keys[player].size() == 17, "incomplete TACTIC set");
  }
  return config;
}
