#pragma once

#include "../share/include.hpp"
#include "../share/types.hpp"
#include "mahjong_util.hpp"

int value_tile_dist(const Tile_Array &hand, const Open_Meld_Vector &open_meld, const int value_tile,
                    const Game_State &game_state);
int half_flush_dist(const Tile_Array &hand, const Open_Meld_Vector &open_meld);
int simples_dist(const Tile_Array &hand, const Open_Meld_Vector &open_meld);
int outside_hand_dist(const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                      const Game_State &game_state);
int all_triplets_dist(const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                      const Game_State &game_state);
int three_color_triplet_dist(const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                             const Game_State &game_state);
int three_color_dist(const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                     const Game_State &game_state);
int full_straight_dist(const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                       const Game_State &game_state);

double calc_yaku_dist(const int my_pid, const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                      const Game_State &game_state);