#include "types.hpp"

#include <cstdio>
#include <numeric>

#include "record_io.hpp"

int next_player(const int pid, const int arg) { return (pid + arg) % 4; }

Color_Type tile_color(const int tile) { return (Color_Type)((tile - 1) / 10); }

Num_Type tile_terminal_or_honor(const int tile) {
  if (tile <= 30) {
    if (tile % 10 == 1 || tile % 10 == 9) {
      return NT_TERMINAL;
    } else {
      return NT_SIMPLES;
    }
  } else {
    return NT_HONOR_TILE;
  }
}

Open_Meld_Elem::Open_Meld_Elem() { target_relative = -1; }

Win_Info::Win_Info() {}

Tenpai_Info::Tenpai_Info() {
  meld_shanten_num = 8;
  seven_pairs_shanten_num =
      6;  // An open-meld hand can need another value, but this value causes no problem.
  win_vec.clear();
}

int Tenpai_Info::shanten_num() const { return std::min(meld_shanten_num, seven_pairs_shanten_num); }

Discarded_Tile::Discarded_Tile() { is_riichi = false; }

Player_State::Player_State() {}

void Player_State::reset_hand_state() {
  riichi_declared = false;
  riichi_accepted = false;
  for (int tile = 0; tile < 38; tile++) {
    hand[tile] = 0;
  }
  open_meld.clear();
  river.clear();
}

void replace_pon_to_upgraded_kan(Open_Meld_Vector &open_meld_vector,
                                 const Open_Meld_Elem open_meld_elem) {
  assert(open_meld_elem.type == FT_UPGRADED_KAN);
  for (int i = 0; i < open_meld_vector.size(); i++) {
    if (open_meld_vector[i].type == FT_PON &&
        tile_kind(open_meld_vector[i].tile) == tile_kind(open_meld_elem.tile)) {
      open_meld_vector[i].type = FT_UPGRADED_KAN;
      open_meld_vector[i].tile = open_meld_elem.tile;
      open_meld_vector[i].consumed = open_meld_elem.consumed;
    }
  }
}

bool Player_State::is_closed_hand() const {
  for (int i = 0; i < open_meld.size(); i++) {
    if (open_meld[i].type != FT_CONCEALED_KAN) {
      return false;
    }
  }
  return true;
}

Game_State::Game_State() {}

void Game_State::reset_all_hand_state() {
  for (int pid = 0; pid < 4; pid++) {
    player_state[pid].reset_hand_state();
  }
}

void Game_State::set_all_self_wind(const int dealer) {
  for (int pid = 0; pid < 4; pid++) {
    player_state[pid].self_wind = (4 + pid - dealer) % 4;
  }
}

Player_Result::Player_Result(int pid_in, int score_in, int self_wind_first_in) {
  pid = pid_in;
  score = score_in;
  self_wind_first = self_wind_first_in;
}

bool Player_Result::operator<(const Player_Result &rhs) const {
  if (score > rhs.score) {
    return true;
  } else if (score == rhs.score && self_wind_first < rhs.self_wind_first) {
    return true;
  } else {
    return false;
  }
}

bool is_last_round(const int round_wind, const int round, const std::string &rule) {
  if (rule == "tonpu") {
    return round_wind == 1 || (round_wind == 0 && round == 4);
  } else if (rule == "tonnan") {
    return round_wind == 2 || (round_wind == 1 && round == 4);
  } else {
    assert_with_out(false, "is_last_round error!");
    return false;
  }
}

bool is_definite_last_round(const int round_wind, const int round, const std::string &rule) {
  if (rule == "tonpu") {
    return round_wind == 1 && round == 4;
  } else if (rule == "tonnan") {
    return round_wind == 2 && round == 4;
  } else {
    assert_with_out(false, "is_definite_last_round error!");
    return false;
  }
}

std::string moves_to_string(const Moves &moves) { return write_game_record(moves); }

void moves_to_file(const Moves &moves, std::string file_name) {
  write_game_record_file(moves, file_name);
}

bool is_valid_player(const int pid) { return 0 <= pid && pid <= 3; }

int tile_kind(const int tile) {
  if (tile % 10 == 0) {
    return tile - 5;
  } else {
    return tile;
  }
}

Tile_Array tile_kind(const Tile_Array &hand) {
  Tile_Array hand_kind = {};
  for (int tile = 1; tile < 38; tile++) {
    hand_kind[tile_kind(tile)] += hand[tile];
  }
  return hand_kind;
}

Open_Meld_Elem tile_kind(Open_Meld_Elem elem) {
  if (elem.type != FT_CONCEALED_KAN) {
    elem.tile = tile_kind(elem.tile);
  }
  for (int i = 0; i < elem.consumed.size(); i++) {
    elem.consumed[i] = tile_kind(elem.consumed[i]);
  }
  return elem;
}

Open_Meld_Vector tile_kind(Open_Meld_Vector open_meld) {
  for (int i = 0; i < open_meld.size(); i++) {
    open_meld[i] = tile_kind(open_meld[i]);
  }
  return open_meld;
}

int dora_to_dora_marker(int tile) {
  if (tile % 10 == 1 && tile < 30) {
    return tile + 8;
  } else if (tile == 31) {
    return 34;
  } else if (tile == 35) {
    return 37;
  } else {
    return tile_kind(tile) - 1;
  }
}

int dora_marker_to_dora(int tile) {
  if (tile % 10 == 9) {
    return tile - 8;
  } else if (tile == 34) {
    return 31;
  } else if (tile == 37) {
    return 35;
  } else {
    return tile_kind(tile) + 1;
  }
}

std::vector<int> dora_marker_to_dora(const std::vector<int> &dora_marker) {
  std::vector<int> dora;
  for (int i = 0; i < dora_marker.size(); i++) {
    dora.push_back(dora_marker_to_dora(dora_marker[i]));
  }
  return dora;
}

int count_dora(const Tile_Array &tile_array, const Open_Meld_Vector &open_meld,
               const std::vector<int> &dora_marker) {
  int dora_num = 0;
  Tile_Array tile_array_kind = tile_kind(tile_array);
  for (int i = 0; i < dora_marker.size(); i++) {
    dora_num += tile_array_kind[dora_marker_to_dora(dora_marker[i])];
    for (int j = 0; j < open_meld.size(); j++) {
      if (open_meld[j].type != FT_CONCEALED_KAN) {
        if (tile_kind(open_meld[j].tile) == dora_marker_to_dora(dora_marker[i])) {
          dora_num++;
        }
      }
      for (int k = 0; k < open_meld[j].consumed.size(); k++) {
        if (tile_kind(open_meld[j].consumed[k]) == dora_marker_to_dora(dora_marker[i])) {
          dora_num++;
        }
      }
    }
  }
  for (int j = 0; j < open_meld.size(); j++) {
    if (open_meld[j].type != FT_CONCEALED_KAN) {
      if (open_meld[j].tile % 10 == 0) {
        dora_num++;
      }
    }
    for (int k = 0; k < open_meld[j].consumed.size(); k++) {
      if (open_meld[j].consumed[k] % 10 == 0) {
        dora_num++;
      }
    }
  }
  return dora_num + tile_array[10] + tile_array[20] + tile_array[30];
}

int count_dora(const Tile_Array &tile_array, const Open_Meld_Vector &open_meld,
               const std::vector<int> &dora_marker,
               const std::vector<int> &underneath_dora_marker) {
  std::vector<int> dora_all;
  dora_all.insert(dora_all.end(), dora_marker.begin(), dora_marker.end());
  dora_all.insert(dora_all.end(), underneath_dora_marker.begin(), underneath_dora_marker.end());
  return count_dora(tile_array, open_meld, dora_all);
}

int get_tile38(const int tile136) {
  if (tile136 == 16) {
    return 10;
  } else if (tile136 == 52) {
    return 20;
  } else if (tile136 == 88) {
    return 30;
  } else {
    int tile_color = tile136 / 36;
    int tile_index = (tile136 % 36) / 4 + 1;
    return 10 * tile_color + tile_index;
  }
}

int parse_wind(const std::string wind) {
  if (wind == "E") {
    return 0;
  } else if (wind == "S") {
    return 1;
  } else if (wind == "W") {
    return 2;
  } else if (wind == "N") {
    return 3;
  } else {
    assert_with_out(false, "parse_wind invalid input");
    return -1;
  }
}

std::string wind_string(const int wind) {
  std::string wind_name = "";
  if (wind == 0) {
    wind_name = "E";
  } else if (wind == 1) {
    wind_name = "S";
  } else if (wind == 2) {
    wind_name = "W";
  } else if (wind == 3) {
    wind_name = "N";
  } else {
    assert_with_out(false, "wind_string invalid input");
  }
  return wind_name;
}

bool is_valid_tile(const int tile) { return 0 < tile && tile < 38; }

int parse_tile(const std::string tile) {
  if (tile == "1m") {
    return 1;
  } else if (tile == "2m") {
    return 2;
  } else if (tile == "3m") {
    return 3;
  } else if (tile == "4m") {
    return 4;
  } else if (tile == "5m") {
    return 5;
  } else if (tile == "6m") {
    return 6;
  } else if (tile == "7m") {
    return 7;
  } else if (tile == "8m") {
    return 8;
  } else if (tile == "9m") {
    return 9;
  } else if (tile == "5mr") {
    return 10;
  } else if (tile == "1p") {
    return 11;
  } else if (tile == "2p") {
    return 12;
  } else if (tile == "3p") {
    return 13;
  } else if (tile == "4p") {
    return 14;
  } else if (tile == "5p") {
    return 15;
  } else if (tile == "6p") {
    return 16;
  } else if (tile == "7p") {
    return 17;
  } else if (tile == "8p") {
    return 18;
  } else if (tile == "9p") {
    return 19;
  } else if (tile == "5pr") {
    return 20;
  } else if (tile == "1s") {
    return 21;
  } else if (tile == "2s") {
    return 22;
  } else if (tile == "3s") {
    return 23;
  } else if (tile == "4s") {
    return 24;
  } else if (tile == "5s") {
    return 25;
  } else if (tile == "6s") {
    return 26;
  } else if (tile == "7s") {
    return 27;
  } else if (tile == "8s") {
    return 28;
  } else if (tile == "9s") {
    return 29;
  } else if (tile == "5sr") {
    return 30;
  } else if (tile == "E") {
    return 31;
  } else if (tile == "S") {
    return 32;
  } else if (tile == "W") {
    return 33;
  } else if (tile == "N") {
    return 34;
  } else if (tile == "P") {
    return 35;
  } else if (tile == "F") {
    return 36;
  } else if (tile == "C") {
    return 37;
  } else if (tile == "?") {
    return -1;
  } else {
    assert_with_out(false, "parse_tile invalid input");
    return 0;
  }
}

std::string tile_string(int tile) {
  std::string tile_name = "";
  if (tile == 1) {
    tile_name = "1m";
  } else if (tile == 2) {
    tile_name = "2m";
  } else if (tile == 3) {
    tile_name = "3m";
  } else if (tile == 4) {
    tile_name = "4m";
  } else if (tile == 5) {
    tile_name = "5m";
  } else if (tile == 6) {
    tile_name = "6m";
  } else if (tile == 7) {
    tile_name = "7m";
  } else if (tile == 8) {
    tile_name = "8m";
  } else if (tile == 9) {
    tile_name = "9m";
  } else if (tile == 10) {
    tile_name = "5mr";
  } else if (tile == 11) {
    tile_name = "1p";
  } else if (tile == 12) {
    tile_name = "2p";
  } else if (tile == 13) {
    tile_name = "3p";
  } else if (tile == 14) {
    tile_name = "4p";
  } else if (tile == 15) {
    tile_name = "5p";
  } else if (tile == 16) {
    tile_name = "6p";
  } else if (tile == 17) {
    tile_name = "7p";
  } else if (tile == 18) {
    tile_name = "8p";
  } else if (tile == 19) {
    tile_name = "9p";
  } else if (tile == 20) {
    tile_name = "5pr";
  } else if (tile == 21) {
    tile_name = "1s";
  } else if (tile == 22) {
    tile_name = "2s";
  } else if (tile == 23) {
    tile_name = "3s";
  } else if (tile == 24) {
    tile_name = "4s";
  } else if (tile == 25) {
    tile_name = "5s";
  } else if (tile == 26) {
    tile_name = "6s";
  } else if (tile == 27) {
    tile_name = "7s";
  } else if (tile == 28) {
    tile_name = "8s";
  } else if (tile == 29) {
    tile_name = "9s";
  } else if (tile == 30) {
    tile_name = "5sr";
  } else if (tile == 31) {
    tile_name = "E";
  } else if (tile == 32) {
    tile_name = "S";
  } else if (tile == 33) {
    tile_name = "W";
  } else if (tile == 34) {
    tile_name = "N";
  } else if (tile == 35) {
    tile_name = "P";
  } else if (tile == 36) {
    tile_name = "F";
  } else if (tile == 37) {
    tile_name = "C";
  } else {
    assert_with_out(false, "tile_string invalid_input");
  }
  return tile_name;
}

int ceil_fu(const int fu) {
  if (fu % 10 == 0 || fu == 25) {
    return fu;
  } else {
    return fu + 10 - (fu % 10);
  }
}

Game_State get_game_state_start_round(const Event &action_json) {
  Game_State game_state;
  assert(action_json.type == EventType::ROUND);
  game_state.reset_all_hand_state();
  const int dealer_id = action_json.dealer;
  game_state.round_wind = action_json.round_wind;
  game_state.round = action_json.round;
  game_state.ranking_model_round = action_json.ranking_model_round;
  game_state.repeat_counter = action_json.repeat_counter;
  game_state.deposit = action_json.deposit;
  game_state.dora_marker.clear();
  game_state.dora_marker.push_back(action_json.dora_indicator);

  for (int pid = 0; pid < 4; pid++) {
    game_state.player_state[pid].self_wind = (4 + pid - dealer_id) % 4;
    game_state.player_state[pid].score = action_json.scores[pid];
    for (const int tile : action_json.hands[pid]) {
      if (is_valid_tile(tile)) {
        game_state.player_state[pid].hand[tile]++;
      }
    }
  }
  return game_state;
}

void go_next_state(Game_State &game_state, const Event &action_json) {
  if (action_json.type == EventType::DORA) {
    game_state.dora_marker.push_back(action_json.dora_indicator);
  } else if (action_json.type == EventType::DRAW) {
    const int tile = action_json.tile;
    if (is_valid_tile(tile)) {
      const int actor = action_json.player;
      game_state.player_state[actor].hand[tile]++;
    }
  } else if (action_json.type == EventType::RIICHI) {
    const int actor = action_json.player;
    game_state.player_state[actor].riichi_declared = true;
  } else if (action_json.type == EventType::RIICHI_ACCEPTED) {
    const int actor = action_json.player;
    game_state.player_state[actor].riichi_accepted = true;
    game_state.player_state[actor].score -= 1000;
    game_state.deposit++;
  } else if (action_json.type == EventType::DISCARD) {
    const int tile = action_json.tile;
    const int actor = action_json.player;
    Discarded_Tile discarded_tile;
    discarded_tile.tile = tile;
    discarded_tile.discard_from_draw = action_json.from_draw;
    discarded_tile.is_riichi = game_state.player_state[actor].riichi_declared &&
                               !game_state.player_state[actor].riichi_accepted;
    game_state.player_state[actor].river.push_back(discarded_tile);
    game_state.player_state[actor].hand[tile]--;
  } else if (action_json.type == EventType::CHII) {
    const int actor = action_json.player;
    const int target = action_json.target;
    for (const int tile : action_json.consumed) {
      game_state.player_state[actor].hand[tile]--;
    }
    Open_Meld_Elem open_meld_elem;
    open_meld_elem.type = FT_CHII;
    open_meld_elem.tile = action_json.tile;
    open_meld_elem.consumed = action_json.consumed;
    open_meld_elem.target_relative = (4 + target - actor) % 4;
    game_state.player_state[actor].open_meld.push_back(open_meld_elem);
  } else if (action_json.type == EventType::PON) {
    const int actor = action_json.player;
    const int target = action_json.target;
    for (const int tile : action_json.consumed) {
      game_state.player_state[actor].hand[tile]--;
    }
    Open_Meld_Elem open_meld_elem;
    open_meld_elem.type = FT_PON;
    open_meld_elem.tile = action_json.tile;
    open_meld_elem.consumed = action_json.consumed;
    open_meld_elem.target_relative = (4 + target - actor) % 4;
    game_state.player_state[actor].open_meld.push_back(open_meld_elem);
  } else if (action_json.type == EventType::OPEN_KAN) {
    const int actor = action_json.player;
    const int target = action_json.target;
    for (const int tile : action_json.consumed) {
      game_state.player_state[actor].hand[tile]--;
    }
    Open_Meld_Elem open_meld_elem;
    open_meld_elem.type = FT_OPEN_KAN;
    open_meld_elem.tile = action_json.tile;
    open_meld_elem.consumed = action_json.consumed;
    open_meld_elem.target_relative = (4 + target - actor) % 4;
    game_state.player_state[actor].open_meld.push_back(open_meld_elem);
  } else if (action_json.type == EventType::CONCEALED_KAN) {
    const int actor = action_json.player;
    for (const int tile : action_json.consumed) {
      game_state.player_state[actor].hand[tile]--;
    }
    Open_Meld_Elem open_meld_elem;
    open_meld_elem.type = FT_CONCEALED_KAN;
    open_meld_elem.consumed = action_json.consumed;
    game_state.player_state[actor].open_meld.push_back(open_meld_elem);
  } else if (action_json.type == EventType::UPGRADED_KAN) {
    const int actor = action_json.player;
    game_state.player_state[actor].hand[action_json.tile]--;
    Open_Meld_Elem open_meld_elem;
    open_meld_elem.type = FT_UPGRADED_KAN;
    open_meld_elem.tile = action_json.tile;
    open_meld_elem.consumed = action_json.consumed;
    replace_pon_to_upgraded_kan(game_state.player_state[actor].open_meld, open_meld_elem);
  }
}

Game_State get_game_state(const Moves &game_record) {
  Game_State game_state;
  int begin = 0;
  for (int i = game_record.size() - 1; 0 <= i; i--) {
    if (game_record[i].type == EventType::ROUND) {
      begin = i;
      break;
    }
  }
  for (int i = begin; i < game_record.size(); i++) {
    const Event &action_json = game_record[i];
    if (action_json.type == EventType::GAME) {
      continue;
    } else if (action_json.type == EventType::WIN) {
      game_state.deposit = 0;
    } else if (action_json.type == EventType::ROUND) {
      game_state = get_game_state_start_round(action_json);
    } else {
      go_next_state(game_state, action_json);
    }
  }
  return game_state;
}

bool is_riichi_accepted(const Moves &game_record, const int pid) {
  for (int i = game_record.size() - 1; 0 <= i; i--) {
    const Event &action_json = game_record[i];
    if (action_json.type == EventType::ROUND) {
      return false;
    }
    if (action_json.type == EventType::RIICHI_ACCEPTED && action_json.player == pid) {
      return true;
    }
  }
  return false;
}

bool is_ippatsu_valid(const Moves &game_record, const int pid) {
  for (int i = game_record.size() - 1; 0 <= i; i--) {
    const Event &action_json = game_record[i];
    if (action_json.type == EventType::ROUND) {
      return false;
    }
    if (action_json.type == EventType::DISCARD && action_json.player == pid) {
      return false;
    }
    if (action_json.type == EventType::PON || action_json.type == EventType::CHII ||
        action_json.type == EventType::OPEN_KAN) {
      if (action_json.target == pid) {
        return false;
      }
    }
    if (action_json.type == EventType::RIICHI_ACCEPTED && action_json.player == pid) {
      return true;
    }
  }
  return false;
}

std::pair<int, int> count_tsumo_num(const Moves &game_record) {
  int tsumo_wall = 0;
  int tsumo_dead_wall_draw = 0;
  for (int i = game_record.size() - 1; 0 <= i; i--) {
    const Event &action_json = game_record[i];
    if (action_json.type == EventType::ROUND) {
      break;
    }
    if (action_json.type == EventType::DRAW) {
      assert(i > 0);
      assert(game_record[i - 1].type != EventType::CONCEALED_KAN);
      if (game_record[i - 1].type == EventType::OPEN_KAN ||
          game_record[i - 1].type == EventType::UPGRADED_KAN ||
          (game_record[i - 1].type == EventType::DORA &&
           game_record[i - 2].type == EventType::CONCEALED_KAN)) {
        tsumo_dead_wall_draw++;
      } else {
        tsumo_wall++;
      }
    }
  }
  assert(tsumo_wall + tsumo_dead_wall_draw <= 70);
  return std::pair<int, int>(tsumo_wall, tsumo_dead_wall_draw);
}

int count_tsumo_num_all(const Moves &game_record) {
  std::pair<int, int> tsumo_num_pair = count_tsumo_num(game_record);
  return tsumo_num_pair.first + tsumo_num_pair.second;
}

bool get_red_dora(const Moves &game_record) {
  assert_with_out(0 < game_record.size(), "get_red_dora: game_record size error");
  assert_with_out(game_record[0].type == EventType::GAME, "get_red_dora: type error");
  return game_record[0].red_dora;
}

int get_round_wind(const Moves &game_record) {
  for (int i = game_record.size() - 1; 0 <= i; i--) {
    const Event &action_json = game_record[i];
    if (action_json.type == EventType::ROUND) {
      return action_json.round_wind;
    }
  }
  return -1;
}

int get_round(const Moves &game_record) {
  for (int i = game_record.size() - 1; 0 <= i; i--) {
    const Event &action_json = game_record[i];
    if (action_json.type == EventType::ROUND) {
      return action_json.round;
    }
  }
  return -1;
}

int get_dealer(const Moves &game_record) {
  for (int i = game_record.size() - 1; 0 <= i; i--) {
    const Event &action_json = game_record[i];
    if (action_json.type == EventType::ROUND) {
      return action_json.dealer;
    }
  }
  return -1;
}

int get_repeat_counter(const Moves &game_record) {
  for (int i = game_record.size() - 1; 0 <= i; i--) {
    const Event &action_json = game_record[i];
    if (action_json.type == EventType::ROUND) {
      return action_json.repeat_counter;
    }
  }
  return 0;
}

int get_deposit(const Moves &game_record) {
  int tmp = 0;
  for (int i = game_record.size() - 1; 0 <= i; i--) {
    const Event &action_json = game_record[i];
    if (action_json.type == EventType::WIN) {
      return 0;
    }
    if (action_json.type == EventType::RIICHI_ACCEPTED) {
      tmp++;
    }
    if (action_json.type == EventType::ROUND) {
      return action_json.deposit + tmp;
    }
  }
  return tmp;
}

bool is_tobi_any(const std::array<int, 4> &scores) {
  for (const int score : scores) {
    if (score < 0) {
      return true;
    }
  }
  return false;
}

bool is_tobi_any(const Event &round_result_move) {
  assert(is_round_result(round_result_move.type));
  return is_tobi_any(round_result_move.scores);
}

bool is_30000_any(const std::array<int, 4> &scores) {
  for (const int score : scores) {
    if (score >= 30000) {
      return true;
    }
  }
  return false;
}

bool is_30000_any(const Event &round_result_move) {
  assert(is_round_result(round_result_move.type));
  return is_30000_any(round_result_move.scores);
}

bool has_highest_score(const std::array<int, 4> &scores, const int pid) {
  for (int pid2 = 0; pid2 < 4; pid2++) {
    if (pid2 != pid && scores[pid2] >= scores[pid]) {
      return false;
    }
  }
  return true;
}

bool has_highest_score(const Event &round_result_move, const int pid) {
  assert(is_round_result(round_result_move.type));
  return has_highest_score(round_result_move.scores, pid);
}

std::pair<int, int> cal_next_round_wind_round(const Moves &game_record) {
  const Event &round_result = game_record[game_record.size() - 1];
  assert(is_round_result(round_result.type));
  const int round_wind = get_round_wind(game_record);
  const int round = get_round(game_record);
  const int dealer = get_dealer(game_record);
  if (round_result.type == EventType::DRAWN_NINE_TERMINALS) {
    return std::pair<int, int>(round_wind,
                               round);  // Nine terminals: replay the same round unconditionally.
  } else if ((round_result.type == EventType::WIN && round_result.player == dealer) ||
             (round_result.type == EventType::DRAWN_EXHAUSTIVE && round_result.ready[dealer])) {
    if (is_last_round(round_wind, round, "tonpu") && round_result.scores[dealer] >= 30000 &&
        has_highest_score(round_result, dealer)) {
      return std::pair<int, int>(-1, -1);
    } else if (is_definite_last_round(round_wind, round, "tonpu") &&
               has_highest_score(round_result, dealer)) {
      return std::pair<int, int>(-1, -1);
    } else {
      return std::pair<int, int>(round_wind, round);
    }
  } else {
    if (is_last_round(round_wind, round, "tonpu") && is_30000_any(round_result)) {
      return std::pair<int, int>(-1, -1);
    } else if (is_definite_last_round(round_wind, round, "tonpu")) {
      return std::pair<int, int>(-1, -1);
    } else {
      return std::pair<int, int>(round == 4 ? round_wind + 1 : round_wind,
                                 round == 4 ? 1 : round + 1);
    }
  }
}

int cal_next_repeat_counter(const Moves &game_record) {
  const Event &round_result = game_record[game_record.size() - 1];
  assert(is_round_result(round_result.type));
  if (round_result.type == EventType::DRAWN_EXHAUSTIVE ||
      round_result.type == EventType::DRAWN_NINE_TERMINALS) {
    return get_repeat_counter(game_record) + 1;
  }
  const int dealer = get_dealer(game_record);
  if (round_result.type == EventType::WIN && round_result.player == dealer) {
    return get_repeat_counter(game_record) + 1;
  }
  return 0;
}

int cal_next_dealer(const Moves &game_record) {
  const Event &round_result = game_record[game_record.size() - 1];
  assert(is_round_result(round_result.type));
  const int dealer = get_dealer(game_record);
  if ((round_result.type == EventType::WIN && round_result.player == dealer) ||
      round_result.type == EventType::DRAWN_NINE_TERMINALS ||
      (round_result.type == EventType::DRAWN_EXHAUSTIVE && round_result.ready[dealer])) {
    return dealer;
  } else {
    return (dealer + 1) % 4;
  }
}

bool is_closed_hand(const Moves &game_record, const int pid) {
  // Check whether the player made pon, chii, or open kan.
  for (int i = game_record.size() - 1; 0 <= i; i--) {
    const Event &action_json = game_record[i];
    if (action_json.type == EventType::ROUND) {
      break;
    }
    if (action_json.type == EventType::PON || action_json.type == EventType::CHII ||
        action_json.type == EventType::OPEN_KAN) {
      if (action_json.player == pid) {
        return false;
      }
    }
  }
  return true;
}

std::array<bool, 38> get_furiten_flags(const Moves &game_record, const Game_State &game_state,
                                       const int pid, const bool skip_latest) {
  // Get the flags of the tiles that cause furiten.
  std::array<bool, 38> furiten_flags;
  std::fill(furiten_flags.begin(), furiten_flags.end(), false);
  bool once_discard = false;
  bool is_riichi = game_state.player_state[pid].riichi_accepted;
  for (int i = game_record.size() - 1; 0 <= i; i--) {
    const Event &action_json = game_record[i];
    if (action_json.type == EventType::ROUND) {
      break;
    }
    if (action_json.type == EventType::DISCARD || action_json.type == EventType::UPGRADED_KAN) {
      if (skip_latest && i == game_record.size() - 1) {
        continue;
      }
      if (action_json.player == pid) {
        furiten_flags[tile_kind(action_json.tile)] = true;
        once_discard = true;
      } else if (!once_discard || is_riichi) {
        furiten_flags[tile_kind(action_json.tile)] = true;
      }
    }
    if (action_json.type == EventType::RIICHI_ACCEPTED && action_json.player == pid) {
      is_riichi = false;
    }
  }
  return furiten_flags;
}

Moves load_game_record_from_file(const std::string &file_name, int length) {
  const Moves events = load_game_record(file_name);
  if (length < 0) {
    length = events.size();
  }
  assert_with_out(length <= int(events.size()), "load_game_record_from_file: length error");
  Moves game_record;
  for (int i = 0; i < length; i++) {
    game_record.push_back(events[i]);
  }
  return game_record;
}
