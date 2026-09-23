#pragma once

#include "../share/types.hpp"
#include "mahjong_util.hpp"

float infer_tenpai_prob_old(const River &river, const int open_meld_num);
float infer_tenpai_prob(const Moves &game_record, const Game_State &game_state, const int target);

bool flushing_possible(const Open_Meld_Vector &open_meld, const Color_Type color);
std::array<int, 4> get_flushing_feature(const River &river, const Color_Type color);
std::array<int, 2> get_flushing_tenpai_post_feature(const River &river, const Color_Type color);
float cal_flushing_prob(const Game_State &game_state, const int target, const Color_Type color);
float cal_flushing_tenpai_post(const Game_State &game_state, const int target,
                               const Color_Type color);