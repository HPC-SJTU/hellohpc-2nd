#pragma once

#include <cstdint>

#include "import.hpp"
#include "share/calc_shanten.hpp"
#include "share/include.hpp"
#include "share/make_move.hpp"
#include "share/types.hpp"
#include "share/win_points.hpp"

struct Game_Origin {
  int dealer;
  bool red_dora;
  int round_wind;
  int round;
  int ranking_model_round;
  int repeat_counter;
  int deposit;
  std::array<int, 4> scores;
};

enum Game_Phase {
  GP_AI,
  GP_PLAYER,
  GP_END,
};

Moves get_masked_log(Moves game_record, const int pid);

void seed_rng(uint64_t seed);

void prepare_wall(std::vector<int> &wall);
std::array<std::array<int, 13>, 4> get_start_hand(const std::vector<int> &wall, const int dealer);
void initialize_game(std::vector<int> &wall, Moves &game_record, const Game_Origin &game_origin);
void add_next_round_or_end_game(Moves &game_record, std::vector<int> &wall, const Event &request);

void add_tsumo(const std::vector<int> &wall, Moves &game_record, const int pid);
void add_dead_wall_draw(const std::vector<int> &wall, Moves &game_record, const int pid);
void add_after_concealed_kan(const std::vector<int> &wall, Moves &game_record, const int pid);
void add_accept_riichi_or_dora_if_necessary(const std::vector<int> &wall, Moves &game_record);
void add_drawn_round_fanpai(Moves &game_record);

void add_move_after_discard(const std::vector<int> &wall, Moves &game_record,
                            const std::array<Moves, 4> &candidate_moves);
void add_move_after_tsumo(const std::vector<int> &wall, Moves &game_record,
                          const std::array<Moves, 4> &candidate_moves);
void add_move_after_tsumo_or_discard(const std::vector<int> &wall, Moves &game_record,
                                     const std::array<Moves, 4> &candidate_moves);

Moves ai_assign(const Moves &game_record, const int player_id);
std::array<Moves, 4> require_moves_after_tsumo(const Moves &game_record, const int player_id,
                                               const Event &request);
std::array<Moves, 4> require_moves_after_discard(const Moves &game_record, const int player_id,
                                                 const Event &request);
std::array<Moves, 4> require_moves_after_tsumo_or_discard(const Moves &game_record,
                                                          const int player_id,
                                                          const Event &request);

void proceed_game(std::vector<int> &wall, Moves &game_record, const int player_id,
                  const Event &request);
void game_loop(std::vector<int> &wall, Moves &game_record, const Game_Origin &game_origin,
               const int player_id);
