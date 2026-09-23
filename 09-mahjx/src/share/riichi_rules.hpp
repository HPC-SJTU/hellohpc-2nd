#pragma once

#include "types.hpp"

bool is_legal_concealed_kan_after_riichi(const Tile_Array &hand_before_draw,
                                         const Open_Meld_Vector &open_meld, int drawn_tile);
bool completing_tile_exists(const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                            int completing_tile);
