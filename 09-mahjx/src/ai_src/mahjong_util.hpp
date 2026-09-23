#pragma once

#include "../share/include.hpp"
#include "../share/types.hpp"

int factorial(int x);
int combination(int n, int k);
float my_logit(const float x);

float logistic(const float *const w, const float *const x, const int DVec);
void MC_logistic(const float *const w, const float *const x, float *p, const int DVec,
                 const int NClass);
void MC_logistic_mod(const float *const w, const float *const x, float *p, const int DVec,
                     const int NClass);

bool is_red_tile(const int tile);

int mod_pid(const int round, const int dealer, const int pid);

int get_other_riichi_declared_num(const int my_pid, const Game_State &game_state);
int get_max_other_open_meld_num(const int my_pid, const Game_State &game_state);

Tile_Array get_tile_visible_all(const Game_State &game_state);
int get_remaining_tile_num(const int my_pid, const Game_State &game_state);
Tile_Array get_tile_visible_wo_hand(const int my_pid, const Game_State &game_state);
Tile_Array get_tile_visible_me(const int my_pid, const Game_State &game_state);

Tile_Array sum_tile_array(const Tile_Array &tile_array1, const Tile_Array &tile_array2);
Tile_Array cal_remaining_kind_array(const Tile_Array &visible_kind);
std::array<bool, 38> get_discard_kind(const River &river);
std::array<bool, 38> get_decline_win(const Moves &game_record, const int target);
std::array<bool, 38> get_decline_win_ar(const Moves &game_record, const Game_State &game_state,
                                        const int target);
std::array<bool, 38> get_discard_before_riichi(const River &river);
int count_hand_discard_num(const River &river);
std::array<std::array<bool, 38>, 38> get_proto_sequence(const Game_State &game_state,
                                                        const int target);
std::array<std::array<std::array<bool, 38>, 7>, 3> get_proto_sequence_open_wait(
    const Game_State &game_state, const int target);
std::array<std::array<std::array<bool, 38>, 9>, 3> get_proto_sequence_kanchan(
    const Game_State &game_state, const int target);
std::array<std::array<std::array<bool, 38>, 2>, 3> get_proto_sequence_edge_wait(
    const Game_State &game_state, const int target);

int tile_dora_han(const std::vector<int> &dora_markerv, int tile);
void han_prob_shift(std::array<float, 14> &han_prob, const int shift_num);
void hanfu_prob_han_shift(std::array<std::array<float, 12>, 14> &hanfu_prob, const int shift_num);
void hanfu_prob_han_shift_with_prob(std::array<std::array<float, 12>, 14> &hanfu_prob,
                                    const std::array<float, 14> &shift_prob);

std::array<std::array<std::array<float, 12>, 14>, 4> cal_hanfu_prob_kan(
    const std::array<std::array<std::array<float, 12>, 14>, 4> &hanfu_prob,
    const std::array<float, 14> &shift_prob);
