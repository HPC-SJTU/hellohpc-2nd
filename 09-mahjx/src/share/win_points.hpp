#pragma once

#include "include.hpp"

int ron_win(const int han, const int fu, const bool is_dealer);
int tsumo_win_loss(const int han, const int fu, const bool is_dealer);
int tsumo_win(const int han, const int fu, const bool is_dealer);
std::array<int, 4> points_move_win(const int who, const int from_who, const int han, const int fu,
                                   const int dealer, const int repeat_multiplier, const int stick);
std::array<int, 4> points_move_drawn_round(const std::array<bool, 4> &is_tenpai);
