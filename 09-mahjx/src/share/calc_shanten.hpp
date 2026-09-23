#pragma once

#include "calc_win.hpp"
#include "include.hpp"
#include "types.hpp"

Tile_Array using_tile_array(const Tile_Array &hand, const Open_Meld_Vector &open_meld);

void tenpai_check(const int round_wind_tile, const int self_wind_tile,
                  const Tile_Array &using_array, const Tile_Array &hand, const Tile_Array &hand_cut,
                  Tile_Array &hand_tmp, const Open_Meld_Vector &open_meld,
                  Tenpai_Info &tenpai_info);

void analyze_proto_sequence(const int round_wind_tile, const int self_wind_tile,
                            const Tile_Array &using_array, const Tile_Array &hand,
                            const Tile_Array &hand_cut, Tile_Array &hand_tmp,
                            const Open_Meld_Vector &open_meld, Tenpai_Info &tenpai_info);

void cut_sequence(const int round_wind_tile, const int self_wind_tile,
                  const Tile_Array &using_array, const Tile_Array &hand, const Tile_Array &hand_cut,
                  Tile_Array &hand_tmp, const Open_Meld_Vector &open_meld,
                  Tenpai_Info &tenpai_info);

void cut_triplet(const int round_wind_tile, const int self_wind_tile, const Tile_Array &using_array,
                 const Tile_Array &hand, Tile_Array &hand_tmp, const int start,
                 const Open_Meld_Vector &open_meld, Tenpai_Info &tenpai_info);

void seven_pairs_shanten(const int round_wind_tile, const int self_wind_tile,
                         const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                         Tenpai_Info &tenpai_info);

void analyze_hand(const int round_wind_tile, const int self_wind_tile, const Tile_Array using_array,
                  const Tile_Array hand, const Open_Meld_Vector open_meld,
                  Tenpai_Info &tenpai_info);

Tenpai_Info cal_tenpai_info(const int round_wind, const int self_wind, const Tile_Array &hand,
                            const Open_Meld_Vector &open_meld);

int count_terminal_or_honor_kind(const Tile_Array &hand);