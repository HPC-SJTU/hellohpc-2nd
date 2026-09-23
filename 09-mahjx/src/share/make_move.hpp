#pragma once

#include "calc_shanten.hpp"
#include "include.hpp"
#include "types.hpp"
#include "win_points.hpp"

Event make_start_game(const int dealer, const bool red_dora);
Event make_start_round(const int round_wind, const int round, const int ranking_model_round,
                       const int repeat_counter, const int deposit, const int dealer,
                       const int dora_marker, const std::array<std::array<int, 13>, 4> &hands,
                       const std::array<int, 4> &scores, const std::vector<int> &wall);
Event make_dora(const int dora_marker);
Event make_none(const int actor);
Event make_tsumo(const int actor, const int tile);

Event make_discard(const int actor, const int tile, const bool discard_from_draw);
Event make_riichi(const int actor);
Event make_riichi_accepted(const int actor);

Event make_pon(const int actor, const int target, const int tile, const std::vector<int> consumed);
Event make_pon_default(const int actor, const int target, const int tile);
Event make_pon_red(const int actor, const int target, const int tile);

Event make_open_kan(const int actor, const int target, const int tile,
                    const std::vector<int> consumed);
Event make_open_kan_default(const int actor, const int target, const int tile);
Event make_open_kan_red(const int actor, const int target, const int tile);

Event make_concealed_kan(const int actor, const std::vector<int> consumed);
Event make_concealed_kan_default(const int actor, const int tile);
Event make_concealed_kan_red(const int actor, const int tile);

Event make_upgraded_kan(const int actor, const int tile, const std::vector<int> consumed);
Event make_upgraded_kan_default(const int actor, const int tile);
Event make_upgraded_kan_red(const int actor, const int tile);

Event make_chii(const int actor, const int target, const int tile, const std::vector<int> consumed);
Event make_chii_low_default(const int actor, const int target, const int tile);
Event make_chii_low_red(const int actor, const int target, const int tile);
Event make_chii_middle_default(const int actor, const int target, const int tile);
Event make_chii_middle_red(const int actor, const int target, const int tile);
Event make_chii_high_default(const int actor, const int target, const int tile);
Event make_chii_high_red(const int actor, const int target, const int tile);

Event make_win(const int actor, const int target, const int tile_win);
Event make_win(const int actor, const int target, const int tile_win, const Tile_Array &hand,
               const int han, const int fu, const std::vector<int> &underneath_dora_markers,
               const std::array<int, 4> &scores);
Event make_drawn_round_fanpai(const std::array<bool, 4> &tenpai,
                              const std::array<std::vector<int>, 4> &hands,
                              const std::array<int, 4> &scores);
Event make_nine_terminals(const int actor);
Event make_nine_terminals(const int actor, const Tile_Array &hand,
                          const std::array<int, 4> &scores);

Event make_end_game(const std::array<int, 4> &scores);

bool is_valid_start_round(const Event &action_json);
bool is_valid_discard(const Event &action_json);
bool is_valid_chii(const Event &action_json);
bool is_valid_pon(const Event &action_json);
bool is_valid_open_kan(const Event &action_json);
bool is_valid_concealed_kan(const Event &action_json);
bool is_valid_upgraded_kan(const Event &action_json);

bool is_valid_riichi(const Event &action_json);
bool is_valid_win(const Event &action_json);

bool is_valid_riichi_and_discard(const Moves &moves);
bool is_valid_pon_and_discard(const Moves &moves);
bool is_valid_chii_and_discard(const Moves &moves);

bool is_valid_game_record(const Moves &game_record);

bool is_legal_discard(const Moves &game_record, const Game_State &game_state,
                      const Event &action_json);
bool is_legal_chii(const Moves &game_record, const Game_State &game_state,
                   const Event &action_json);
bool is_legal_pon(const Moves &game_record, const Game_State &game_state, const Event &action_json);
bool is_legal_open_kan(const Moves &game_record, const Game_State &game_state,
                       const Event &action_json);
bool is_legal_concealed_kan(const Moves &game_record, const Game_State &game_state,
                            const Event &action_json);
bool is_legal_upgraded_kan(const Moves &game_record, const Game_State &game_state,
                           const Event &action_json);

bool is_legal_win(const Moves &game_record, const Game_State &game_state, const Event &action_json);

bool is_legal_riichi_and_discard(const Moves &game_record, const Game_State &game_state,
                                 const Moves &moves);
bool is_legal_pon_and_discard(const Moves &game_record, const Game_State &game_state,
                              const Moves &moves);
bool is_legal_chii_and_discard(const Moves &game_record, const Game_State &game_state,
                               const Moves &moves);

bool is_legal_nine_terminals(const Moves &game_record, const Game_State &game_state,
                             const Event &action_json);

bool is_legal_none(const Moves &game_record, const Event &action_json);
bool is_legal_riichi(const Moves &game_record, const Game_State &game_state,
                     const Event &action_json);
bool is_legal_single_move(const Moves &game_record, const Event &action_json);
bool is_legal_discard_after_riichi_or_open_meld(const Moves &game_record, const Event &action_json);

std::vector<Moves> get_legal_discard_from_draw_move(const Moves &game_record);
std::vector<Moves> get_legal_hand_discard_move(const Moves &game_record);
std::vector<Moves> get_legal_pon_discard_move(const Moves &game_record);
std::vector<Moves> get_legal_chii_discard_move(const Moves &game_record);
std::vector<Moves> get_legal_open_kan_move(const Moves &game_record);
std::vector<Moves> get_legal_concealed_kan_move(const Moves &game_record);
std::vector<Moves> get_legal_upgraded_kan_move(const Moves &game_record);
std::vector<Moves> get_legal_riichi_discard_move(const Moves &game_record);
std::vector<Moves> get_legal_tsumo_win_move(const Moves &game_record);
std::vector<Moves> get_legal_ron_move(const Moves &game_record);
std::array<std::vector<Moves>, 4> get_all_legal_moves(const Moves &game_record);
std::vector<Event> get_all_legal_single_action(const Moves &game_record);
