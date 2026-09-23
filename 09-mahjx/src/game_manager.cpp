#include "game_manager.hpp"

#include <cstdint>
#include <cstdio>

#include "share/response_priority.hpp"

namespace {
uint64_t g_rng_state[4];

uint64_t rotl64(const uint64_t x, const int k) { return (x << k) | (x >> (64 - k)); }

uint64_t splitmix64(uint64_t &x) {
  x += 0x9E3779B97F4A7C15ULL;
  uint64_t z = x;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

// xoshiro256++ (Blackman, Vigna).
uint64_t next_rng() {
  const uint64_t result = rotl64(g_rng_state[1] * 5, 7) * 9;
  const uint64_t t = g_rng_state[1] << 17;
  g_rng_state[2] ^= g_rng_state[0];
  g_rng_state[3] ^= g_rng_state[1];
  g_rng_state[1] ^= g_rng_state[2];
  g_rng_state[0] ^= g_rng_state[3];
  g_rng_state[2] ^= t;
  g_rng_state[3] = rotl64(g_rng_state[3], 45);
  return result;
}
}  // namespace

void seed_rng(const uint64_t seed) {
  uint64_t sm = seed;
  for (int i = 0; i < 4; i++) {
    g_rng_state[i] = splitmix64(sm);
  }
}

// Fisher-Yates.
void shuffle_vector(std::vector<int> &v) {
  for (int i = (int)v.size() - 1; 0 < i; i--) {
    const int j = (int)(next_rng() % (uint64_t)(i + 1));
    std::swap(v[i], v[j]);
  }
}

Moves get_masked_log(Moves game_record, const int pid) {
  // Get the log with the draw tiles of other players hidden.
  for (int i = 0; i < game_record.size(); i++) {
    if (game_record[i].type == EventType::DRAW && game_record[i].player != pid) {
      game_record[i].tile = -1;
    }
  }
  return game_record;
}

void prepare_wall(std::vector<int> &wall) {
  wall.clear();
  const int wall_size = 136;
  for (int i = 0; i < wall_size; i++) {
    wall.push_back(i);
  }

  shuffle_vector(wall);

  for (int i = 0; i < wall_size; i++) {
    wall[i] = get_tile38(wall[i]);
  }
}

std::array<std::array<int, 13>, 4> get_start_hand(const std::vector<int> &wall, const int dealer) {
  std::array<std::array<int, 13>, 4> start_hand_tmp, start_hand;
  for (int i = 0; i < 13 * 4; i++) {
    int pid = i % 4;
    start_hand_tmp[pid][i / 4] = wall[i];
  }
  for (int pid = 0; pid < 4; pid++) {
    start_hand[pid] = start_hand_tmp[(4 + pid - dealer) % 4];
  }
  return start_hand;
}

void initialize_game(std::vector<int> &wall, Moves &game_record, const Game_Origin &game_origin) {
  assert(game_record.empty());
  assert(is_valid_player(game_origin.dealer));
  assert(0 <= game_origin.round_wind && game_origin.round_wind < 4);
  assert(1 <= game_origin.round && game_origin.round <= 4);
  assert(0 <= game_origin.ranking_model_round && game_origin.ranking_model_round < 12);
  assert(0 <= game_origin.repeat_counter);
  assert(0 <= game_origin.deposit);

  prepare_wall(wall);
  Event game = make_start_game(game_origin.dealer, game_origin.red_dora);
  game.round_wind = game_origin.round_wind;
  game.round = game_origin.round;
  game.ranking_model_round = game_origin.ranking_model_round;
  game.repeat_counter = game_origin.repeat_counter;
  game.deposit = game_origin.deposit;
  game.scores = game_origin.scores;
  game_record.push_back(game);
  const std::array<std::array<int, 13>, 4> start_hand = get_start_hand(wall, game_origin.dealer);
  const int dora_marker = wall[wall.size() - 6];
  game_record.push_back(
      make_start_round(game_origin.round_wind, game_origin.round, game_origin.ranking_model_round,
                       game_origin.repeat_counter, game_origin.deposit, game_origin.dealer,
                       dora_marker, start_hand, game_origin.scores, wall));
}

void add_next_round_or_end_game(Moves &game_record, std::vector<int> &wall, const Event &request) {
  assert(0 < game_record.size());
  const Event &prev = game_record[game_record.size() - 1];
  assert(is_round_result(prev.type));
  std::pair<int, int> next_round_wind_round = cal_next_round_wind_round(game_record);
  const std::array<int, 4> scores = prev.scores;
  if (next_round_wind_round.first == -1) {
    game_record.push_back(make_end_game(scores));
  } else {
    const int next_repeat_counter = cal_next_repeat_counter(game_record);
    const int next_dealer = cal_next_dealer(game_record);
    const Game_State game_state = get_game_state(game_record);
    const int next_ranking_model_round =
        game_state.ranking_model_round + (next_dealer == get_dealer(game_record) ? 0 : 1);
    if (request.wall.empty()) {
      prepare_wall(wall);
    } else {
      wall = request.wall;
    }
    const std::array<std::array<int, 13>, 4> start_hand = get_start_hand(wall, next_dealer);
    const int dora_marker = wall[wall.size() - 6];

    game_record.push_back(make_start_round(next_round_wind_round.first,
                                           next_round_wind_round.second, next_ranking_model_round,
                                           next_repeat_counter, get_deposit(game_record),
                                           next_dealer, dora_marker, start_hand, scores, wall));
  }
}

void add_tsumo(const std::vector<int> &wall, Moves &game_record, const int pid) {
  game_record.push_back(make_tsumo(pid, wall[13 * 4 + count_tsumo_num(game_record).first]));
}

void add_dead_wall_draw(const std::vector<int> &wall, Moves &game_record, const int pid) {
  game_record.push_back(
      make_tsumo(pid, wall[wall.size() - 1 - count_tsumo_num(game_record).second]));
  // The way of taking the dead wall draw differs from real mahjong. It is troublesome, so accept
  // this.
}

void add_after_concealed_kan(const std::vector<int> &wall, Moves &game_record, const int pid) {
  Game_State game_state = get_game_state(game_record);
  const int dora_marker = wall[wall.size() - 6 - 2 * game_state.dora_marker.size()];
  game_record.push_back(make_dora(dora_marker));
  add_dead_wall_draw(wall, game_record, pid);
}

void add_accept_riichi_or_dora_if_necessary(const std::vector<int> &wall, Moves &game_record) {
  assert(game_record[game_record.size() - 1].type == EventType::DISCARD);
  if (game_record[game_record.size() - 2].type == EventType::RIICHI) {
    game_record.push_back(make_riichi_accepted(game_record[game_record.size() - 2].player));
  } else if (game_record[game_record.size() - 2].type == EventType::DRAW) {
    if (game_record[game_record.size() - 3].type == EventType::OPEN_KAN ||
        game_record[game_record.size() - 3].type == EventType::UPGRADED_KAN) {
      Game_State game_state = get_game_state(game_record);
      const int dora_marker = wall[wall.size() - 6 - 2 * game_state.dora_marker.size()];
      game_record.push_back(make_dora(dora_marker));
    }
  }
}

void add_drawn_round_fanpai(Moves &game_record) {
  std::array<bool, 4> is_tenpai = {false, false, false, false};
  std::array<std::vector<int>, 4> hands;
  Game_State game_state = get_game_state(game_record);
  for (int pid = 0; pid < 4; pid++) {
    Tile_Array hand = game_state.player_state[pid].hand;
    const Tenpai_Info tenpai_info =
        cal_tenpai_info(game_state.round_wind, game_state.player_state[pid].self_wind, hand,
                        game_state.player_state[pid].open_meld);
    if (tenpai_info.shanten_num() == 0) {
      is_tenpai[pid] = true;
      for (int tile = 1; tile < 38; ++tile)
        for (int n = 0; n < hand[tile]; ++n) hands[pid].push_back(tile);
    } else {
      for (int tile = 0; tile < 38; tile++) {
        for (int i = 0; i < hand[tile]; i++) {
          hands[pid].push_back(-1);
        }
      }
    }
  }
  std::array<int, 4> deltas = points_move_drawn_round(is_tenpai);
  std::array<int, 4> scores;
  for (int pid = 0; pid < 4; pid++) {
    scores[pid] = game_state.player_state[pid].score + deltas[pid];
  }
  game_record.push_back(make_drawn_round_fanpai(is_tenpai, hands, scores));
}

void add_move_after_discard(const std::vector<int> &wall, Moves &game_record,
                            const std::array<Moves, 4> &candidate_moves) {
  const Event &current_move = game_record[game_record.size() - 1];
  assert(current_move.type == EventType::DISCARD || current_move.type == EventType::UPGRADED_KAN);
  const int target = current_move.player;
  const int win_actor = first_win_responder(target, candidate_moves);
  if (win_actor != -1) {
    const int actor = win_actor;
    assert(actor == candidate_moves[actor][0].player);
    assert(target == candidate_moves[actor][0].target);
    const int tile = candidate_moves[actor][0].tile;
    const Game_State game_state = get_game_state(game_record);
    Tile_Array hand = game_state.player_state[actor].hand;
    const Tenpai_Info tenpai_info =
        cal_tenpai_info(game_state.round_wind, game_state.player_state[actor].self_wind, hand,
                        game_state.player_state[actor].open_meld);
    int fu_index = -1;
    int win_points = 0;
    int han_add = 0;
    if (game_state.player_state[actor].riichi_accepted) {
      han_add++;
      if (is_ippatsu_valid(game_record, actor)) {
        han_add++;
      }
    }
    if (current_move.type == EventType::UPGRADED_KAN) {
      han_add++;  // ron win on an upgraded kan
    }
    // Last draw.
    hand[tile]++;
    int dora_num = 0;
    std::vector<int> underneath_dora_marker;
    if (game_state.player_state[actor].riichi_accepted) {
      for (int i = 0; i < game_state.dora_marker.size(); i++) {
        underneath_dora_marker.push_back(wall[136 - 5 - 2 * i]);
      }
    }
    dora_num = count_dora(hand, game_state.player_state[actor].open_meld, game_state.dora_marker,
                          underneath_dora_marker);

    const int last_discard_han = (count_tsumo_num_all(game_record) == 70 ? 1 : 0);
    for (int i = 0; i < tenpai_info.win_vec.size(); i++) {
      if (tile_kind(tile) == tenpai_info.win_vec[i].tile &&
          han_add + last_discard_han + tenpai_info.win_vec[i].han_ron > 0) {
        int win_points_tmp =
            ron_win(han_add + last_discard_han + tenpai_info.win_vec[i].han_ron + dora_num,
                    tenpai_info.win_vec[i].fu_ron, game_state.player_state[actor].self_wind == 0);
        if (win_points_tmp > win_points) {
          win_points = win_points_tmp;
          fu_index = i;
        }
      }
    }
    assert(fu_index != -1);  // A declared win must match a confirmed win record.
    const int han = han_add + last_discard_han + tenpai_info.win_vec[fu_index].han_ron + dora_num;
    const int fu = tenpai_info.win_vec[fu_index].fu_ron;
    std::array<int, 4> points_move =
        points_move_win(actor, target, han, fu, get_dealer(game_record), game_state.repeat_counter,
                        get_deposit(game_record));
    std::array<int, 4> scores;
    for (int pid = 0; pid < 4; pid++) {
      scores[pid] = game_state.player_state[pid].score + points_move[pid];
    }
    hand[tile]--;
    game_record.push_back(
        make_win(actor, target, tile, hand, han, fu, underneath_dora_marker, scores));
    return;
  }
  if (count_tsumo_num_all(game_record) == 70) {
    add_drawn_round_fanpai(game_record);
    return;
  } else if (current_move.type == EventType::UPGRADED_KAN) {
    // The last tile cannot make an upgraded kan, so count_tsumo_num_all(game_record) cannot be 70.
    add_dead_wall_draw(wall, game_record, target);
    return;
  }
  for (int pid_add = 1; pid_add <= 3; pid_add++) {
    const int actor = (target + pid_add) % 4;
    if (candidate_moves[actor][0].type == EventType::PON) {
      add_accept_riichi_or_dora_if_necessary(wall, game_record);
      for (const Event &action : candidate_moves[actor]) {
        game_record.push_back(action);
      }
      return;
    } else if (candidate_moves[actor][0].type == EventType::OPEN_KAN) {
      add_accept_riichi_or_dora_if_necessary(wall, game_record);
      game_record.push_back(candidate_moves[actor][0]);
      return;
    }
  }
  for (int pid_add = 1; pid_add <= 1; pid_add++) {
    const int actor = (target + pid_add) % 4;
    if (candidate_moves[actor][0].type == EventType::CHII) {
      add_accept_riichi_or_dora_if_necessary(wall, game_record);
      for (const Event &action : candidate_moves[actor]) {
        game_record.push_back(action);
      }
      return;
    }
  }
  add_accept_riichi_or_dora_if_necessary(wall, game_record);
  add_tsumo(wall, game_record, (target + 1) % 4);
}

void add_move_after_tsumo(const std::vector<int> &wall, Moves &game_record,
                          const std::array<Moves, 4> &candidate_moves) {
  assert(game_record[game_record.size() - 1].type == EventType::DRAW);
  const Moves &moves = candidate_moves[game_record[game_record.size() - 1].player];
  if (moves[0].type == EventType::WIN) {
    const int actor = moves[0].player;
    const int tile = moves[0].tile;
    const Game_State game_state = get_game_state(game_record);
    Tile_Array hand = game_state.player_state[actor].hand;
    hand[tile]--;
    const Tenpai_Info tenpai_info =
        cal_tenpai_info(game_state.round_wind, game_state.player_state[actor].self_wind, hand,
                        game_state.player_state[actor].open_meld);
    int fu_index = -1;
    int win_points = 0;
    int han_add = 0;
    if (game_state.player_state[actor].riichi_accepted) {
      han_add++;
      if (is_ippatsu_valid(game_record, actor)) {
        han_add++;
      }
    }
    hand[tile]++;
    int dora_num = 0;
    std::vector<int> underneath_dora_marker;
    if (game_state.player_state[actor].riichi_accepted) {
      for (int i = 0; i < game_state.dora_marker.size(); i++) {
        underneath_dora_marker.push_back(wall[136 - 5 - 2 * i]);
      }
    }
    dora_num = count_dora(hand, game_state.player_state[actor].open_meld, game_state.dora_marker,
                          underneath_dora_marker);

    // The last draw of the wall gives one han.
    const int last_draw_han = (count_tsumo_num_all(game_record) == 70 ? 1 : 0);
    for (int i = 0; i < tenpai_info.win_vec.size(); i++) {
      if (tile_kind(tile) == tenpai_info.win_vec[i].tile &&
          han_add + tenpai_info.win_vec[i].han_tsumo + last_draw_han > 0) {
        int win_points_tmp = tsumo_win(
            han_add + tenpai_info.win_vec[i].han_tsumo + last_draw_han + dora_num,
            tenpai_info.win_vec[i].fu_tsumo, game_state.player_state[actor].self_wind == 0);
        if (win_points_tmp > win_points) {
          win_points = win_points_tmp;
          fu_index = i;
        }
      }
    }
    assert(fu_index != -1);  // A declared win must match a confirmed win record.
    const int han = han_add + tenpai_info.win_vec[fu_index].han_tsumo + last_draw_han + dora_num;
    const int fu = tenpai_info.win_vec[fu_index].fu_tsumo;
    std::array<int, 4> points_move =
        points_move_win(actor, actor, han, fu, get_dealer(game_record), game_state.repeat_counter,
                        get_deposit(game_record));
    std::array<int, 4> scores;
    for (int pid = 0; pid < 4; pid++) {
      scores[pid] = game_state.player_state[pid].score + points_move[pid];
    }
    hand[tile]--;
    game_record.push_back(
        make_win(actor, actor, tile, hand, han, fu, underneath_dora_marker, scores));
  } else if (moves[0].type == EventType::NINE_TERMINALS) {
    const int actor = moves[0].player;
    const Game_State game_state = get_game_state(game_record);
    std::array<int, 4> scores;
    for (int pid = 0; pid < 4; pid++) {
      scores[pid] = game_state.player_state[pid].score;
    }
    game_record.push_back(make_nine_terminals(actor, game_state.player_state[actor].hand, scores));
  } else {
    for (int i = 0; i < moves.size(); i++) {
      game_record.push_back(moves[i]);
    }
  }
}

void add_move_after_tsumo_or_discard(const std::vector<int> &wall, Moves &game_record,
                                     const std::array<Moves, 4> &candidate_moves) {
  const Event &current_move = game_record[game_record.size() - 1];
  if (current_move.type == EventType::DRAW) {
    add_move_after_tsumo(wall, game_record, candidate_moves);
  } else if (current_move.type == EventType::DISCARD ||
             current_move.type == EventType::UPGRADED_KAN) {
    add_move_after_discard(wall, game_record, candidate_moves);
  } else {
    assert(false);
  }
}

Moves ai_assign(const Moves &game_record, const int player_id) {
  return ai(game_record, player_id, false);
}

std::array<Moves, 4> require_moves_after_discard(const Moves &game_record, const int player_id,
                                                 const Event &request) {
  std::array<std::vector<Moves>, 4> all_legal_moves = get_all_legal_moves(game_record);
  std::array<Moves, 4> result;
  for (int pid = 0; pid < 4; pid++) {
    if (all_legal_moves[pid].size() > 0) {
      if (pid != player_id) {
        result[pid] = ai_assign(game_record, pid);
      } else {
        result[pid].push_back(request);
      }
    } else {
      Moves moves;
      moves.push_back(make_none(pid));
      result[pid] = moves;
    }
  }
  return result;
}

std::array<Moves, 4> require_moves_after_tsumo(const Moves &game_record, const int player_id,
                                               const Event &request) {
  std::array<Moves, 4> result;
  const int actor = game_record[game_record.size() - 1].player;
  if (actor != player_id) {
    result[actor] = ai_assign(game_record, actor);
  } else {
    result[actor].push_back(request);
  }
  return result;
}

std::array<Moves, 4> require_moves_after_tsumo_or_discard(const Moves &game_record,
                                                          const int player_id,
                                                          const Event &request) {
  const Event &current_move = game_record[game_record.size() - 1];
  if (current_move.type == EventType::DRAW) {
    return require_moves_after_tsumo(game_record, player_id, request);
  } else if (current_move.type == EventType::DISCARD ||
             current_move.type == EventType::UPGRADED_KAN) {
    return require_moves_after_discard(game_record, player_id, request);
  } else {
    assert_with_out(false, "require_moves_after_tsumo_or_discard error");
    std::array<Moves, 4> result;
    return result;
  }
}

void proceed_game(std::vector<int> &wall, Moves &game_record, const int player_id,
                  const Event &request) {
  const size_t prev_size = game_record.size();
  assert(!game_record.empty());
  {
    const Event &current_move = game_record[game_record.size() - 1];
    if (is_round_result(current_move.type)) {
      add_next_round_or_end_game(game_record, wall, request);
      assert_with_out(wall.size() == 136, "proceed_game_error: wall.size() != 136");
    } else if (current_move.type == EventType::ROUND) {
      add_tsumo(wall, game_record, get_dealer(game_record));
    } else if (current_move.type == EventType::DRAW || current_move.type == EventType::DISCARD ||
               current_move.type == EventType::UPGRADED_KAN) {
      if (request.type != EventType::PASS && !is_legal_single_move(game_record, request)) {
        return;
      }
      std::array<Moves, 4> candidate_moves =
          require_moves_after_tsumo_or_discard(game_record, player_id, request);
      add_move_after_tsumo_or_discard(wall, game_record, candidate_moves);
    } else if (current_move.type == EventType::CONCEALED_KAN) {
      add_after_concealed_kan(wall, game_record, current_move.player);
    } else if (current_move.type == EventType::OPEN_KAN) {
      add_dead_wall_draw(wall, game_record, current_move.player);
    } else if (current_move.type == EventType::RIICHI || current_move.type == EventType::CHII ||
               current_move.type == EventType::PON) {
      assert_with_out(current_move.player == player_id, "unexpected_actor");
      if (is_legal_discard_after_riichi_or_open_meld(game_record, request)) {
        game_record.push_back(request);
      }
    }
  }
  if (player_id == -1) {
    for (size_t i = prev_size; i < game_record.size(); i++) {
      std::cout << i << " " << event_type_text(game_record[i].type) << std::endl;
    }
  }
}

void game_loop(std::vector<int> &wall, Moves &game_record, const Game_Origin &game_origin,
               const int player_id) {
  initialize_game(wall, game_record, game_origin);
  while (game_record.back().type != EventType::END_GAME) {
    proceed_game(wall, game_record, player_id, Event());
  }
}
