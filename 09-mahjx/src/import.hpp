#pragma once

#include "ai_src/selector.hpp"
#include "share/include.hpp"
#include "share/types.hpp"

Moves ai(const Moves &game_record, const int pid, const bool console_out_input);
void set_tactics(const std::array<Tactics, 4> &tactics);
std::vector<std::pair<Moves, float>> calc_moves_score(const Moves &game_record, const int pid);
std::vector<Decision_Score> ai_review(const Moves &game_record, const int pid);
