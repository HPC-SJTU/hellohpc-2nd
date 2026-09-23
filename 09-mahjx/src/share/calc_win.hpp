#pragma once

#include "calc_yaku.hpp"
#include "include.hpp"
#include "types.hpp"

Win_Info calc_win(const int round_wind_tile, const int self_wind_tile, const Tile_Array &hand,
                  const Tile_Array &hand_cut, const Tile_Array &hand_tmp,
                  const Open_Meld_Vector &open_meld, const int wait_tile, const Wait_Type wait_type,
                  const bool seven_pairs);
