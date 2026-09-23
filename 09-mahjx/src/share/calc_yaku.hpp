#pragma once

#include "include.hpp"
#include "types.hpp"

bool closed_hand_check(const Open_Meld_Vector &open_meld);
bool value_tile_check(const int value_tile, const Tile_Array &hand_rest,
                      const Open_Meld_Vector &open_meld, const int wait_tile,
                      const Wait_Type wait_type);
bool pinfu_check(const Tile_Array &hand_rest, const Open_Meld_Vector &open_meld,
                 const Wait_Type wait_type, const int round_wind_tile, const int self_wind_tile);
bool full_flush_check(const Tile_Array &hand, const Open_Meld_Vector &open_meld);
bool half_flush_check(const Tile_Array &hand, const Open_Meld_Vector &open_meld);
bool simples_check(const Tile_Array &hand, const Open_Meld_Vector &open_meld, const int wait_tile);
bool terminals_and_honors_check(const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                                const int wait_tile);
bool suitsu_check(const Tile_Array &hand, const Open_Meld_Vector &open_meld);

void add_hand_sequence_to_sequence(Tile_Array &hand_sequence, Tile_Array &sequence);

bool pure_outside_hand_check(const Tile_Array &hand, const Tile_Array &hand_rest,
                             const Open_Meld_Vector &open_meld, const Tile_Array &hand_tmp,
                             const int wait_tile);
bool outside_hand_check(const Tile_Array &hand, const Tile_Array &hand_rest,
                        const Open_Meld_Vector &open_meld, const Tile_Array &hand_tmp,
                        const int wait_tile);
bool all_triplets_check(const Tile_Array &hand_cut, const Open_Meld_Vector &open_meld,
                        const Tile_Array &hand_tmp, const Wait_Type wait_type);
int count_pure_double_sequences(const Tile_Array &hand_cut, const Open_Meld_Vector &open_meld,
                                const Tile_Array &hand_tmp, const int wait_tile,
                                const Wait_Type wait_type);
bool three_color_sequence_check(const Tile_Array &hand_cut, const Open_Meld_Vector &open_meld,
                                const Tile_Array &hand_tmp, const int wait_tile,
                                const Wait_Type wait_type);
bool three_color_triplet_check(const Tile_Array &hand_rest, const Open_Meld_Vector &open_meld,
                               const int wait_tile, const Wait_Type wait_type);
bool full_straight_check(const Tile_Array &hand_cut, const Open_Meld_Vector &open_meld,
                         const Tile_Array &hand_tmp, const int wait_tile,
                         const Wait_Type wait_type);
bool nine_gates_check(const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                      const int wait_tile);
int concealed_triplet_num_count(const Tile_Array &hand_rest, const Open_Meld_Vector &open_meld);
int kan_num_count(const Open_Meld_Vector &open_meld);

std::pair<int, int> calc_fu(const int round_wind_tile, const int self_wind_tile,
                            const Tile_Array &hand, const Tile_Array &hand_cut,
                            const Tile_Array &hand_rest, const Tile_Array &hand_tmp,
                            const Open_Meld_Vector &open_meld, const int wait_tile,
                            const Wait_Type wait_type, const bool closed_hand, const bool pinfu,
                            const bool seven_pairs);