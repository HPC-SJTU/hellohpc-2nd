#pragma once

#include "../share/include.hpp"
#include "../share/types.hpp"

int value_tile_check_pattern(int meld[4][3], int round_wind_tile, int self_wind_tile);
int full_flush_check_pattern(int meld[4][3], int head[2], std::vector<int> tile_in);
int half_flush_check_pattern(int meld[4][3], int head[2], std::vector<int> tile_in);
int simples_check_pattern(int meld[4][3], int head[2], std::vector<int> tile_in);
int all_triplets_check_pattern(int meld[4][3], int head[2], std::vector<int> tile_in);
