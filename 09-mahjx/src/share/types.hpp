#pragma once

#include "event.hpp"
#include "include.hpp"

int next_player(const int pid, const int arg);

enum Color_Type {
  CT_CHARACTERS = 0,
  CT_DOTS = 1,
  CT_BAMBOO = 2,
  CT_HONOR_TILE = 3,
  CT_NUM = 4,
};
ENABLE_ENUM_OPERATORS(Color_Type)

enum Num_Type {
  NT_SIMPLES = 0,
  NT_TERMINAL = 1,
  NT_HONOR_TILE = 2,
};
ENABLE_ENUM_OPERATORS(Num_Type)

enum Open_Meld_Type {
  FT_CHII = 1,
  FT_PON = 2,            // pon
  FT_OPEN_KAN = 3,       // open kan
  FT_CONCEALED_KAN = 4,  // concealed kan
  FT_UPGRADED_KAN = 5,   // upgraded kan
};

enum Wait_Type {
  MT_OPEN_WAIT = 0,
  MT_DUAL_PAIR = 1,
  MT_PAIR_WAIT = 2,
  MT_KANCHAN = 3,
  MT_EDGE_WAIT = 4,
};

Color_Type tile_color(const int tile);
Num_Type tile_terminal_or_honor(const int tile);

class Open_Meld_Elem {
 public:
  Open_Meld_Type type;
  int tile;
  std::vector<int> consumed;
  int target_relative;  // 1: next player, 2: opposite player, 3: previous player. Store the
                        // relative position. It is convenient for image display.

  Open_Meld_Elem();
};

class Win_Info {
 public:
  int tile;
  int han_tsumo;
  int fu_tsumo;
  int han_ron;
  int fu_ron;

  Win_Info();
};

class Tenpai_Info {
 public:
  int meld_shanten_num;
  int seven_pairs_shanten_num;
  std::vector<Win_Info> win_vec;

  Tenpai_Info();
  int shanten_num() const;
};

class Discarded_Tile {
 public:
  int tile;
  bool discard_from_draw;
  bool is_riichi;

  Discarded_Tile();
};

using Tile_Array = std::array<int, 38>;
using Open_Meld_Vector = std::vector<Open_Meld_Elem>;
using River = std::vector<Discarded_Tile>;

void replace_pon_to_upgraded_kan(Open_Meld_Vector &open_meld_vector,
                                 const Open_Meld_Elem open_meld_elem);

class Player_State {
 public:
  int score;
  int self_wind;  // 0,1,2,3 : East, South, West, North
  Tile_Array hand;
  Open_Meld_Vector open_meld;
  River river;
  bool riichi_declared;
  bool riichi_accepted;

  Player_State();
  void reset_hand_state();
  bool is_closed_hand() const;
};

class Game_State {
 public:
  int round_wind;  // 0,1,2,3 : East, South, West, North
  int round;       // 1,2,3,4
  int ranking_model_round;
  int repeat_counter;
  int deposit;
  std::vector<int> dora_marker;
  Player_State player_state[4];

  Game_State();
  void reset_all_hand_state();
  void set_all_self_wind(const int dealer);
};

class Player_Result {
 public:
  Player_Result(int pid_in, int score_in, int self_wind_first_in);
  bool operator<(const Player_Result &rhs) const;
  int pid;
  int score;
  int self_wind_first;
};

std::string moves_to_string(const Moves &moves);
void moves_to_file(const Moves &moves, std::string file_name);

bool is_last_round(const int round_wind, const int round, const std::string &rule);
bool is_definite_last_round(const int round_wind, const int round, const std::string &rule);

bool is_valid_player(const int pid);

int tile_kind(const int tile);  // Return the tile kind. Values 10, 20, and 30 are the red five of
                                // characters, dots, and bamboo, respectively.
Tile_Array tile_kind(const Tile_Array &hand);
Open_Meld_Elem tile_kind(Open_Meld_Elem elem);
Open_Meld_Vector tile_kind(Open_Meld_Vector open_meld);

int dora_to_dora_marker(const int tile);
int dora_marker_to_dora(const int tile);
std::vector<int> dora_marker_to_dora(const std::vector<int> &dora_marker);

int count_dora(const Tile_Array &Tile_Array, const Open_Meld_Vector &open_meld,
               const std::vector<int> &dora_marker);
int count_dora(const Tile_Array &Tile_Array, const Open_Meld_Vector &open_meld,
               const std::vector<int> &dora_marker, const std::vector<int> &underneath_dora_marker);

int get_tile38(const int tile136);

int parse_wind(const std::string wind);
std::string wind_string(const int wind);

bool is_valid_tile(const int tile);
int parse_tile(const std::string tile);
std::string tile_string(const int tile);

int ceil_fu(const int fu);

Game_State get_game_state_start_round(const Event &action_json);
void go_next_state(Game_State &game_state, const Event &action_json);
Game_State get_game_state(const Moves &game_record);

bool is_riichi_accepted(const Moves &game_record, const int pid);
bool is_ippatsu_valid(const Moves &game_record, const int pid);
std::pair<int, int> count_tsumo_num(const Moves &game_record);
int count_tsumo_num_all(const Moves &game_record);

bool get_red_dora(const Moves &game_record);

int get_round_wind(const Moves &game_record);
int get_round(const Moves &game_record);
int get_dealer(const Moves &game_record);
int get_repeat_counter(const Moves &game_record);
int get_deposit(const Moves &game_record);

bool is_tobi_any(const std::array<int, 4> &scores);
bool is_tobi_any(const Event &round_result_move);
bool is_30000_any(const std::array<int, 4> &scores);
bool is_30000_any(const Event &round_result_move);
bool has_highest_score(const std::array<int, 4> &scores, const int pid);
bool has_highest_score(const Event &round_result_move, const int pid);

std::pair<int, int> cal_next_round_wind_round(const Moves &game_record);
int cal_next_repeat_counter(const Moves &game_record);
int cal_next_dealer(const Moves &game_record);

bool is_closed_hand(const Moves &game_record, const int pid);
std::array<bool, 38> get_furiten_flags(const Moves &game_record, const Game_State &game_state,
                                       const int pid, const bool skip_latest);

Moves load_game_record_from_file(const std::string &file_name, int length);
