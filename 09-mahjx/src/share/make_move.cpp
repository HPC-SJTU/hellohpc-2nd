#include "make_move.hpp"

#include "riichi_rules.hpp"

Event make_start_game(const int dealer, const bool red_dora) {
  Event move;
  move.type = EventType::GAME;
  move.dealer = dealer;
  move.red_dora = red_dora;
  return move;
}

Event make_start_round(const int round_wind, const int round, const int ranking_model_round,
                       const int repeat_counter, const int deposit, const int dealer,
                       const int dora_marker, const std::array<std::array<int, 13>, 4> &hands,
                       const std::array<int, 4> &scores, const std::vector<int> &wall) {
  Event move;
  move.type = EventType::ROUND;
  move.round_wind = round_wind;
  move.round = round;
  move.ranking_model_round = ranking_model_round;
  move.repeat_counter = repeat_counter;
  move.deposit = deposit;
  move.dealer = dealer;
  move.dora_indicator = dora_marker;
  for (int pid = 0; pid < 4; pid++) {
    move.hands[pid].assign(hands[pid].begin(), hands[pid].end());
  }
  move.scores = scores;
  move.wall = wall;
  return move;
}

Event make_dora(const int dora_marker) {
  Event move;
  move.type = EventType::DORA;
  move.dora_indicator = dora_marker;
  return move;
}

Event make_none(const int actor) {
  Event move;
  move.type = EventType::PASS;
  move.player = actor;
  return move;
}

Event make_tsumo(const int actor, const int tile) {
  Event move;
  move.type = EventType::DRAW;
  move.player = actor;
  move.tile = tile;
  return move;
}

Event make_discard(const int actor, const int tile, const bool discard_from_draw) {
  Event move;
  move.type = EventType::DISCARD;
  move.player = actor;
  move.tile = tile;
  move.from_draw = discard_from_draw;
  return move;
}

Event make_riichi(const int actor) {
  Event move;
  move.type = EventType::RIICHI;
  move.player = actor;
  return move;
}

Event make_riichi_accepted(const int actor) {
  Event move;
  move.type = EventType::RIICHI_ACCEPTED;
  move.player = actor;
  return move;
}

Event make_pon(const int actor, const int target, const int tile, const std::vector<int> consumed) {
  Event move;
  move.type = EventType::PON;
  move.player = actor;
  move.target = target;
  move.tile = tile;
  move.consumed = consumed;
  return move;
}

Event make_pon_default(const int actor, const int target, const int tile) {
  return make_pon(actor, target, tile, std::vector<int>({tile_kind(tile), tile_kind(tile)}));
}

Event make_pon_red(const int actor, const int target, const int tile) {
  assert(tile % 10 == 5 && tile < 30);
  return make_pon(actor, target, tile, std::vector<int>({tile_kind(tile), tile_kind(tile) + 5}));
}

Event make_open_kan(const int actor, const int target, const int tile,
                    const std::vector<int> consumed) {
  Event move;
  move.type = EventType::OPEN_KAN;
  move.player = actor;
  move.target = target;
  move.tile = tile;
  move.consumed = consumed;
  return move;
}

Event make_open_kan_default(const int actor, const int target, const int tile) {
  return make_open_kan(actor, target, tile,
                       std::vector<int>({tile_kind(tile), tile_kind(tile), tile_kind(tile)}));
}

Event make_open_kan_red(const int actor, const int target, const int tile) {
  assert(tile % 10 == 5 && tile < 30);
  return make_open_kan(actor, target, tile,
                       std::vector<int>({tile_kind(tile), tile_kind(tile), tile_kind(tile) + 5}));
}

Event make_concealed_kan(const int actor, const std::vector<int> consumed) {
  Event move;
  move.type = EventType::CONCEALED_KAN;
  move.player = actor;
  move.consumed = consumed;
  return move;
}

Event make_concealed_kan_default(const int actor, const int tile) {
  return make_concealed_kan(actor, std::vector<int>({tile_kind(tile), tile_kind(tile),
                                                     tile_kind(tile), tile_kind(tile)}));
}

Event make_concealed_kan_red(const int actor, const int tile) {
  assert(tile % 10 == 5 && tile < 30);
  return make_concealed_kan(actor, std::vector<int>({tile_kind(tile), tile_kind(tile),
                                                     tile_kind(tile), tile_kind(tile) + 5}));
}

Event make_upgraded_kan(const int actor, const int tile, const std::vector<int> consumed) {
  Event move;
  move.type = EventType::UPGRADED_KAN;
  move.player = actor;
  move.tile = tile;
  move.consumed = consumed;
  return move;
}

Event make_upgraded_kan_default(const int actor, const int tile) {
  return make_upgraded_kan(actor, tile,
                           std::vector<int>({tile_kind(tile), tile_kind(tile), tile_kind(tile)}));
}

Event make_upgraded_kan_red(const int actor, const int tile) {
  assert(tile % 10 == 5 && tile < 30);
  return make_upgraded_kan(
      actor, tile, std::vector<int>({tile_kind(tile), tile_kind(tile), tile_kind(tile) + 5}));
}

Event make_chii(const int actor, const int target, const int tile,
                const std::vector<int> consumed) {
  Event move;
  move.type = EventType::CHII;
  move.player = actor;
  move.target = target;
  move.tile = tile;
  move.consumed = consumed;
  return move;
}

Event make_chii_low_default(const int actor, const int target, const int tile) {
  assert(tile_kind(tile) < 30);
  assert(tile_kind(tile) % 10 <= 7);
  return make_chii(actor, target, tile,
                   std::vector<int>({tile_kind(tile) + 1, tile_kind(tile) + 2}));
}

Event make_chii_low_red(const int actor, const int target, const int tile) {
  assert(tile_kind(tile) < 30);
  assert(tile_kind(tile) % 10 == 3 || tile_kind(tile) % 10 == 4);
  if (tile_kind(tile) % 10 == 3) {
    return make_chii(actor, target, tile,
                     std::vector<int>({tile_kind(tile) + 1, tile_kind(tile) + 7}));
  } else {
    return make_chii(actor, target, tile,
                     std::vector<int>({tile_kind(tile) + 6, tile_kind(tile) + 2}));
  }
}

Event make_chii_middle_default(const int actor, const int target, const int tile) {
  assert(tile_kind(tile) < 30);
  assert(2 <= tile_kind(tile) % 10 && tile_kind(tile) % 10 <= 8);
  return make_chii(actor, target, tile,
                   std::vector<int>({tile_kind(tile) - 1, tile_kind(tile) + 1}));
}

Event make_chii_middle_red(const int actor, const int target, const int tile) {
  assert(tile_kind(tile) < 30);
  assert(tile_kind(tile) % 10 == 4 || tile_kind(tile) % 10 == 6);
  if (tile_kind(tile) % 10 == 4) {
    return make_chii(actor, target, tile,
                     std::vector<int>({tile_kind(tile) - 1, tile_kind(tile) + 6}));
  } else {
    return make_chii(actor, target, tile,
                     std::vector<int>({tile_kind(tile) + 4, tile_kind(tile) + 1}));
  }
}

Event make_chii_high_default(const int actor, const int target, const int tile) {
  assert(tile_kind(tile) < 30);
  assert(3 <= tile_kind(tile) % 10);
  return make_chii(actor, target, tile,
                   std::vector<int>({tile_kind(tile) - 2, tile_kind(tile) - 1}));
}

Event make_chii_high_red(const int actor, const int target, const int tile) {
  assert(tile_kind(tile) < 30);
  assert(tile_kind(tile) % 10 == 6 || tile_kind(tile) % 10 == 7);
  if (tile_kind(tile) % 10 == 6) {
    return make_chii(actor, target, tile,
                     std::vector<int>({tile_kind(tile) - 2, tile_kind(tile) + 4}));
  } else {
    return make_chii(actor, target, tile,
                     std::vector<int>({tile_kind(tile) + 3, tile_kind(tile) - 1}));
  }
}

Event make_win(const int actor, const int target, const int tile_win) {
  Event move;
  move.type = EventType::WIN;
  move.player = actor;
  move.target = target;
  move.tile = tile_win;
  return move;
}

Event make_win(const int actor, const int target, const int tile_win, const Tile_Array &hand,
               const int han, const int fu, const std::vector<int> &underneath_dora_markers,
               const std::array<int, 4> &scores) {
  Event move = make_win(actor, target, tile_win);
  for (int tile = 1; tile < 38; ++tile)
    for (int n = 0; n < hand[tile]; ++n) move.hand.push_back(tile);
  move.han = han;
  move.fu = fu;
  move.scores = scores;
  move.underneath_dora_indicators = underneath_dora_markers;
  return move;
}

Event make_drawn_round_fanpai(const std::array<bool, 4> &tenpai,
                              const std::array<std::vector<int>, 4> &hands,
                              const std::array<int, 4> &scores) {
  Event move;
  move.type = EventType::DRAWN_EXHAUSTIVE;
  move.ready = tenpai;
  move.hands = hands;
  move.scores = scores;
  return move;
}

Event make_nine_terminals(const int actor) {
  Event move;
  move.type = EventType::NINE_TERMINALS;
  move.player = actor;
  return move;
}

Event make_nine_terminals(const int actor, const Tile_Array &hand,
                          const std::array<int, 4> &scores) {
  Event move;
  move.type = EventType::DRAWN_NINE_TERMINALS;
  move.player = actor;
  for (int pid = 0; pid < 4; pid++) {
    if (pid == actor) {
      for (int tile = 1; tile < 38; ++tile)
        for (int n = 0; n < hand[tile]; ++n) move.hands[pid].push_back(tile);
    } else {
      move.hands[pid].assign(13, -1);
    }
  }
  move.scores = scores;
  return move;
}

Event make_end_game(const std::array<int, 4> &scores) {
  Event move;
  move.type = EventType::END_GAME;
  move.scores = scores;
  return move;
}

bool is_valid_start_round(const Event &action_json) {
  if (action_json.type != EventType::ROUND) {
    return false;
  }
  // TODO: Validate all required fields and value ranges.
  return true;
}

bool is_valid_discard(const Event &action_json) { return action_json.type == EventType::DISCARD; }

bool is_valid_chii(const Event &action_json) {
  if (action_json.type != EventType::CHII) {
    return false;
  }
  if (action_json.player != (action_json.target + 1) % 4) {
    return false;
  }
  if (action_json.consumed.size() != 2) {
    return false;
  }
  const int tile = action_json.tile;
  const int consumed0 = action_json.consumed[0];
  const int consumed1 = action_json.consumed[1];
  return (tile_kind(consumed0) == tile_kind(tile) + 1 &&
          tile_kind(consumed1) == tile_kind(tile) + 2) ||
         (tile_kind(consumed0) == tile_kind(tile) - 1 &&
          tile_kind(consumed1) == tile_kind(tile) + 1) ||
         (tile_kind(consumed0) == tile_kind(tile) - 2 &&
          tile_kind(consumed1) == tile_kind(tile) - 1);
}

bool is_valid_pon(const Event &action_json) {
  if (action_json.type != EventType::PON) {
    return false;
  }
  if (action_json.player == action_json.target) {
    return false;
  }
  if (action_json.consumed.size() != 2) {
    return false;
  }
  const int tile = action_json.tile;
  const int consumed0 = action_json.consumed[0];
  const int consumed1 = action_json.consumed[1];
  return tile_kind(consumed0) == tile_kind(tile) && tile_kind(consumed1) == tile_kind(tile);
}

bool is_valid_open_kan(const Event &action_json) {
  if (action_json.type != EventType::OPEN_KAN) {
    return false;
  }
  if (action_json.player == action_json.target) {
    return false;
  }
  if (action_json.consumed.size() != 3) {
    return false;
  }
  const int tile = action_json.tile;
  const int consumed0 = action_json.consumed[0];
  const int consumed1 = action_json.consumed[1];
  const int consumed2 = action_json.consumed[2];
  return tile_kind(consumed0) == tile_kind(tile) && tile_kind(consumed1) == tile_kind(tile) &&
         tile_kind(consumed2) == tile_kind(tile);
}

bool is_valid_concealed_kan(const Event &action_json) {
  if (action_json.type != EventType::CONCEALED_KAN) {
    return false;
  }
  if (action_json.consumed.size() != 4) {
    return false;
  }
  const int consumed0 = action_json.consumed[0];
  const int consumed1 = action_json.consumed[1];
  const int consumed2 = action_json.consumed[2];
  const int consumed3 = action_json.consumed[3];
  return tile_kind(consumed0) == tile_kind(consumed1) &&
         tile_kind(consumed0) == tile_kind(consumed2) &&
         tile_kind(consumed0) == tile_kind(consumed3);
}

bool is_valid_upgraded_kan(const Event &action_json) {
  if (action_json.type != EventType::UPGRADED_KAN) {
    return false;
  }
  if (action_json.consumed.size() != 3) {
    return false;
  }
  const int tile = action_json.tile;
  const int consumed0 = action_json.consumed[0];
  const int consumed1 = action_json.consumed[1];
  const int consumed2 = action_json.consumed[2];
  return tile_kind(consumed0) == tile_kind(tile) && tile_kind(consumed1) == tile_kind(tile) &&
         tile_kind(consumed2) == tile_kind(tile);
}

bool is_valid_riichi(const Event &action_json) { return action_json.type == EventType::RIICHI; }

bool is_valid_win(const Event &action_json) { return action_json.type == EventType::WIN; }

bool is_valid_riichi_and_discard(const Moves &moves) {
  if (moves.size() != 2) {
    return false;
  }
  return is_valid_riichi(moves[0]) && is_valid_discard(moves[1]) &&
         moves[0].player == moves[1].player;
}

bool is_valid_pon_and_discard(const Moves &moves) {
  if (moves.size() != 2) {
    return false;
  }
  if (!is_valid_pon(moves[0])) {
    return false;
  }
  if (!is_valid_discard(moves[1])) {
    return false;
  }
  if (moves[0].player != moves[1].player) {
    return false;
  }
  return tile_kind(moves[0].tile) != tile_kind(moves[1].tile);
}

bool is_valid_chii_and_discard(const Moves &moves) {
  if (moves.size() != 2) {
    return false;
  }
  if (!is_valid_chii(moves[0])) {
    return false;
  }
  if (!is_valid_discard(moves[1])) {
    return false;
  }
  if (moves[0].player != moves[1].player) {
    return false;
  }
  const int tile = moves[0].tile;
  const int consumed0 = moves[0].consumed[0];
  const int consumed1 = moves[0].consumed[1];
  const int discard = moves[1].tile;
  if (tile_kind(tile) == tile_kind(discard)) {
    return false;
  }
  if (tile_kind(tile) < tile_kind(consumed0) && tile_kind(tile) < tile_kind(consumed1) &&
      tile_kind(discard) == tile_kind(tile) + 3) {
    return false;
  }
  if (tile_kind(tile) > tile_kind(consumed0) && tile_kind(tile) > tile_kind(consumed1) &&
      tile_kind(discard) == tile_kind(tile) - 3) {
    return false;
  }
  return true;
}

bool is_valid_nine_terminals(const Event &action_json) {
  return action_json.type == EventType::NINE_TERMINALS;
}

bool is_valid_game_record(const Moves &game_record) {
  if (game_record.size() < 2) {
    return false;
  }
  for (int i = 0; i < game_record.size(); i++) {
    if (i == 0 && game_record[i].type != EventType::GAME) {
      return false;
    }
    if (i == 1 && !is_valid_start_round(game_record[i])) {
      return false;
    }
  }
  return true;
}

bool is_legal_discard(const Moves &game_record, const Game_State &game_state,
                      const Event &action_json) {
  if (action_json.type != EventType::DISCARD) {
    return false;
  }

  const Event &last_action = game_record[game_record.size() - 1];
  if (last_action.type == EventType::DRAW) {
    const int actor = action_json.player;
    if (actor != last_action.player) {
      return false;
    }
    // Discarding the drawn tile is always possible.
    if (action_json.from_draw) {
      return action_json.tile == last_action.tile;
    }
    // After riichi, only a discard of the drawn tile is possible.
    if (game_state.player_state[actor].riichi_accepted) {
      return false;
    }

    Tile_Array hand_prev = game_state.player_state[actor].hand;
    hand_prev[last_action.tile]--;
    return hand_prev[action_json.tile] > 0;
  }
  return false;
}

bool is_legal_chii(const Moves &game_record, const Game_State &game_state,
                   const Event &action_json) {
  if (!is_valid_chii(action_json)) {
    return false;
  }

  const Event &last_action = game_record[game_record.size() - 1];
  if (last_action.type != EventType::DISCARD) {
    return false;
  }
  if (last_action.player != action_json.target) {
    return false;
  }
  if (last_action.tile != action_json.tile) {
    return false;
  }

  const int actor = action_json.player;
  if (game_state.player_state[actor].riichi_accepted) {
    return false;
  }
  if (count_tsumo_num_all(game_record) == 70) {
    return false;
  }

  const int consumed0 = action_json.consumed[0];
  const int consumed1 = action_json.consumed[1];
  return game_state.player_state[actor].hand[consumed0] > 0 &&
         game_state.player_state[actor].hand[consumed1] > 0;
}

bool is_legal_pon(const Moves &game_record, const Game_State &game_state,
                  const Event &action_json) {
  if (!is_valid_pon(action_json)) {
    return false;
  }

  const Event &last_action = game_record[game_record.size() - 1];
  if (last_action.type != EventType::DISCARD) {
    return false;
  }
  if (last_action.player != action_json.target) {
    return false;
  }
  if (last_action.tile != action_json.tile) {
    return false;
  }

  const int actor = action_json.player;
  if (game_state.player_state[actor].riichi_accepted) {
    return false;
  }
  if (count_tsumo_num_all(game_record) == 70) {
    return false;
  }

  const int consumed0 = action_json.consumed[0];
  const int consumed1 = action_json.consumed[1];
  if (consumed0 == consumed1) {
    return game_state.player_state[actor].hand[consumed0] > 1;
  } else {
    return game_state.player_state[actor].hand[consumed0] > 0 &&
           game_state.player_state[actor].hand[consumed1] > 0;
  }
}

bool is_legal_open_kan(const Moves &game_record, const Game_State &game_state,
                       const Event &action_json) {
  if (!is_valid_open_kan(action_json)) {
    return false;
  }

  const Event &last_action = game_record[game_record.size() - 1];
  if (last_action.type != EventType::DISCARD) {
    return false;
  }
  if (last_action.player != action_json.target) {
    return false;
  }
  if (last_action.tile != action_json.tile) {
    return false;
  }

  const int actor = action_json.player;
  if (game_state.player_state[actor].riichi_accepted) {
    return false;
  }
  if (count_tsumo_num_all(game_record) == 70) {
    return false;
  }

  const int consumed0 = action_json.consumed[0];
  const int consumed1 = action_json.consumed[1];
  const int consumed2 = action_json.consumed[2];
  if (consumed1 == consumed0 && consumed2 == consumed0) {
    return game_state.player_state[actor].hand[consumed0] > 2;
  } else {
    return game_state.player_state[actor].hand[tile_kind(consumed0)] > 1 &&
           game_state.player_state[actor].hand[tile_kind(consumed0) + 5] > 0;
  }
}

bool is_legal_concealed_kan(const Moves &game_record, const Game_State &game_state,
                            const Event &action_json) {
  if (!is_valid_concealed_kan(action_json)) {
    return false;
  }

  const Event &last_action = game_record[game_record.size() - 1];
  if (last_action.type == EventType::DRAW) {
    const int actor = action_json.player;
    if (actor != last_action.player) {
      return false;
    }
    if (count_tsumo_num_all(game_record) > 70) {
      return false;
    }  // A concealed kan is not possible on the last draw.
    Tile_Array hand = game_state.player_state[actor].hand;
    for (const int consumed : action_json.consumed) hand[consumed]--;
    for (const int consumed : action_json.consumed)
      if (hand[consumed] < 0) return false;
    if (game_state.player_state[actor].riichi_accepted) {
      if (tile_kind(action_json.consumed[0]) != tile_kind(last_action.tile)) return false;
      Tile_Array hand_before_draw = game_state.player_state[actor].hand;
      hand_before_draw[last_action.tile]--;
      if (!is_legal_concealed_kan_after_riichi(
              hand_before_draw, game_state.player_state[actor].open_meld, last_action.tile)) {
        return false;
      }
    }
    return true;
  }
  return false;
}

bool is_legal_upgraded_kan(const Moves &game_record, const Game_State &game_state,
                           const Event &action_json) {
  if (!is_valid_upgraded_kan(action_json)) {
    return false;
  }

  const Event &last_action = game_record[game_record.size() - 1];
  if (last_action.type == EventType::DRAW) {
    const int actor = action_json.player;
    if (actor != last_action.player) {
      return false;
    }
    if (count_tsumo_num_all(game_record) > 70) {
      return false;
    }  // An upgraded kan is not possible on the last draw.

    const int tile = action_json.tile;
    Open_Meld_Vector open_meld = game_state.player_state[actor].open_meld;
    for (const auto &elem : open_meld) {
      if (elem.type == FT_PON && tile_kind(elem.tile) == tile_kind(tile)) {
        std::vector<int> pon_tile_vec, consumed_vec;
        pon_tile_vec.push_back(elem.tile);
        pon_tile_vec.push_back(elem.consumed[0]);
        pon_tile_vec.push_back(elem.consumed[1]);
        std::sort(pon_tile_vec.begin(), pon_tile_vec.end());
        consumed_vec.assign(action_json.consumed.begin(), action_json.consumed.end());
        std::sort(consumed_vec.begin(), consumed_vec.end());
        return pon_tile_vec[0] == consumed_vec[0] && pon_tile_vec[1] == consumed_vec[1] &&
               pon_tile_vec[2] == consumed_vec[2] && game_state.player_state[actor].hand[tile] > 0;
      }
    }
  }
  return false;
}

bool is_legal_win(const Moves &game_record, const Game_State &game_state,
                  const Event &action_json) {
  if (!is_valid_win(action_json)) {
    return false;
  }
  const int actor = action_json.player;
  const int tile = action_json.tile;
  const Event &last_action = game_record[game_record.size() - 1];
  int han_add = 0;
  if (game_state.player_state[actor].riichi_accepted) {
    han_add++;
  }
  if (count_tsumo_num_all(game_record) == 70) {
    han_add++;
  }
  if (last_action.type == EventType::DRAW) {
    if (last_action.player != actor) {
      return false;
    }
    if (last_action.tile != tile) {
      return false;
    }
    Tile_Array hand = game_state.player_state[actor].hand;
    hand[tile]--;
    const Tenpai_Info tenpai_info =
        cal_tenpai_info(game_state.round_wind, game_state.player_state[actor].self_wind, hand,
                        game_state.player_state[actor].open_meld);
    for (int i = 0; i < tenpai_info.win_vec.size(); i++) {
      if (tile_kind(tile) == tenpai_info.win_vec[i].tile &&
          han_add + tenpai_info.win_vec[i].han_tsumo > 0) {
        return true;
      }
    }
  } else if (last_action.type == EventType::DISCARD ||
             last_action.type == EventType::UPGRADED_KAN) {
    if (last_action.player == actor) {
      return false;
    }
    if (last_action.tile != tile) {
      return false;
    }
    if (last_action.type == EventType::UPGRADED_KAN) {
      han_add++;
    }
    const Tenpai_Info tenpai_info = cal_tenpai_info(
        game_state.round_wind, game_state.player_state[actor].self_wind,
        game_state.player_state[actor].hand, game_state.player_state[actor].open_meld);
    for (int i = 0; i < tenpai_info.win_vec.size(); i++) {
      if (tile_kind(tile) == tenpai_info.win_vec[i].tile &&
          han_add + tenpai_info.win_vec[i].han_ron > 0) {
        std::array<bool, 38> furiten_flags =
            get_furiten_flags(game_record, game_state, actor, true);
        for (int j = 0; j < tenpai_info.win_vec.size(); j++) {
          if (furiten_flags[tenpai_info.win_vec[j].tile]) {
            return false;
          }
        }
        return true;
      }
    }
  }
  return false;
}

bool is_legal_riichi_and_discard(const Moves &game_record, const Game_State &game_state,
                                 const Moves &moves) {
  if (!is_valid_riichi_and_discard(moves)) {
    return false;
  }
  if (!is_legal_discard(game_record, game_state, moves[1])) {
    return false;
  }

  const int actor = moves[0].player;
  if (game_state.player_state[actor].riichi_declared ||
      game_state.player_state[actor].riichi_accepted) {
    return false;
  }
  if (!game_state.player_state[actor].is_closed_hand()) {
    return false;
  }

  const int tile = moves[1].tile;
  Tile_Array hand = game_state.player_state[actor].hand;
  hand[tile]--;
  Tenpai_Info tenpai_info =
      cal_tenpai_info(game_state.round_wind, game_state.player_state[actor].self_wind, hand,
                      game_state.player_state[actor].open_meld);
  return tenpai_info.win_vec.size() > 0;
}

bool is_legal_pon_and_discard(const Moves &game_record, const Game_State &game_state,
                              const Moves &moves) {
  if (!is_valid_pon_and_discard(moves)) {
    return false;
  }
  if (!is_legal_pon(game_record, game_state, moves[0])) {
    return false;
  }

  const int consumed0 = moves[0].consumed[0];
  const int consumed1 = moves[0].consumed[1];
  const int discard = moves[1].tile;
  Tile_Array hand = game_state.player_state[moves[0].player].hand;
  hand[consumed0]--;
  hand[consumed1]--;
  hand[discard]--;
  return 0 <= hand[consumed0] && 0 <= hand[consumed1] && 0 <= hand[discard];
}

bool is_legal_chii_and_discard(const Moves &game_record, const Game_State &game_state,
                               const Moves &moves) {
  if (!is_valid_chii_and_discard(moves)) {
    return false;
  }
  if (!is_legal_chii(game_record, game_state, moves[0])) {
    return false;
  }

  const int consumed0 = moves[0].consumed[0];
  const int consumed1 = moves[0].consumed[1];
  const int discard = moves[1].tile;
  Tile_Array hand = game_state.player_state[moves[0].player].hand;
  hand[consumed0]--;
  hand[consumed1]--;
  hand[discard]--;
  return 0 <= hand[consumed0] && 0 <= hand[consumed1] && 0 <= hand[discard];
}

bool is_legal_nine_terminals(const Moves &game_record, const Game_State &game_state,
                             const Event &action_json) {
  if (!is_valid_nine_terminals(action_json)) {
    return false;
  }

  const Event &last_action = game_record[game_record.size() - 1];
  if (last_action.type == EventType::DRAW) {
    const int actor = action_json.player;
    if (actor != last_action.player) {
      return false;
    }
    if (game_state.player_state[actor].river.size() > 0) {
      return false;
    }
    for (int pid = 0; pid < 4; pid++) {
      if (game_state.player_state[pid].open_meld.size() > 0) {
        return false;
      }
    }
    if (9 <= count_terminal_or_honor_kind(game_state.player_state[actor].hand)) {
      return true;
    }
  }
  return false;
}

bool is_legal_none(const Moves &game_record, const Event &action_json) {
  if (action_json.type != EventType::PASS) {
    return false;
  }

  const Event &last_action = game_record.back();
  const int last_actor = last_action.player;
  const int actor = action_json.player;
  if (last_actor == actor) {
    if (last_action.type == EventType::DRAW || last_action.type == EventType::RIICHI ||
        last_action.type == EventType::CHII || last_action.type == EventType::PON) {
      return false;
    }
  }
  return true;
}

bool is_legal_riichi(const Moves &game_record, const Game_State &game_state,
                     const Event &action_json) {
  const int actor = action_json.player;
  for (int tile = 1; tile < 38; tile++) {
    for (int discard_from_draw = 0; discard_from_draw < 2; discard_from_draw++) {
      Moves moves = {action_json};
      moves.push_back(make_discard(actor, tile, discard_from_draw == 1));
      if (is_legal_riichi_and_discard(game_record, game_state, moves)) {
        return true;
      }
    }
  }
  return false;
}

bool is_legal_single_move(const Moves &game_record, const Event &action_json) {
  const Game_State game_state = get_game_state(game_record);
  if (action_json.type == EventType::PASS) {
    return is_legal_none(game_record, action_json);
  } else if (action_json.type == EventType::DISCARD) {
    return is_legal_discard(game_record, game_state, action_json);
  } else if (action_json.type == EventType::RIICHI) {
    return is_legal_riichi(game_record, game_state, action_json);
  } else if (action_json.type == EventType::CHII) {
    return is_legal_chii(game_record, game_state, action_json);
  } else if (action_json.type == EventType::PON) {
    return is_legal_pon(game_record, game_state, action_json);
  } else if (action_json.type == EventType::OPEN_KAN) {
    return is_legal_open_kan(game_record, game_state, action_json);
  } else if (action_json.type == EventType::CONCEALED_KAN) {
    return is_legal_concealed_kan(game_record, game_state, action_json);
  } else if (action_json.type == EventType::UPGRADED_KAN) {
    return is_legal_upgraded_kan(game_record, game_state, action_json);
  } else if (action_json.type == EventType::WIN) {
    return is_legal_win(game_record, game_state, action_json);
  } else if (action_json.type == EventType::NINE_TERMINALS) {
    return is_legal_nine_terminals(game_record, game_state, action_json);
  } else {
    return false;
  }
}

bool is_legal_discard_after_riichi_or_open_meld(const Moves &game_record,
                                                const Event &action_json) {
  assert(0 < game_record.size());
  Moves gr = game_record;
  Moves moves;
  moves.push_back(gr.back());
  moves.push_back(action_json);
  gr.pop_back();
  const Game_State game_state = get_game_state(gr);
  if (moves[0].type == EventType::RIICHI) {
    return is_legal_riichi_and_discard(gr, game_state, moves);
  } else if (moves[0].type == EventType::CHII) {
    return is_legal_chii_and_discard(gr, game_state, moves);
  } else if (moves[0].type == EventType::PON) {
    return is_legal_pon_and_discard(gr, game_state, moves);
  } else {
    return false;
  }
}

std::vector<Moves> get_legal_discard_from_draw_move(const Moves &game_record) {
  std::vector<Moves> result;
  const Event &action_json = game_record[game_record.size() - 1];
  if (action_json.type != EventType::DRAW) {
    return result;
  }
  Moves moves;
  moves.push_back(make_discard(action_json.player, action_json.tile, true));
  result.push_back(moves);
  return result;
}

std::vector<Moves> get_legal_hand_discard_move(const Moves &game_record) {
  std::vector<Moves> result;
  const Event &action_json = game_record[game_record.size() - 1];
  if (action_json.type != EventType::DRAW) {
    return result;
  }

  Game_State game_state = get_game_state(game_record);
  const int actor = action_json.player;
  for (int tile = 1; tile < 38; tile++) {
    Moves moves;
    Event hand_discard_move = make_discard(actor, tile, false);
    if (is_legal_discard(game_record, game_state, hand_discard_move)) {
      moves.push_back(hand_discard_move);
      result.push_back(moves);
    }
  }
  return result;
}

std::vector<Moves> get_legal_pon_discard_move(const Moves &game_record) {
  std::vector<Moves> result;
  const Event &action_json = game_record[game_record.size() - 1];
  if (action_json.type != EventType::DISCARD) {
    return result;
  }
  Game_State game_state = get_game_state(game_record);
  const int target = action_json.player;
  const int tile_target = action_json.tile;
  for (int pid = 0; pid < 4; pid++) {
    if (pid != target) {
      Event pon_move = make_pon_default(pid, target, tile_target);
      for (int tile = 1; tile < 38; tile++) {
        Moves moves;
        Event hand_discard_move = make_discard(pid, tile, false);
        moves.push_back(pon_move);
        moves.push_back(hand_discard_move);
        if (is_legal_pon_and_discard(game_record, game_state, moves)) {
          result.push_back(moves);
        }
      }
      if (tile_target % 10 == 5 && tile_target < 30) {
        pon_move = make_pon_red(pid, target, tile_target);
        for (int tile = 1; tile < 38; tile++) {
          Moves moves;
          Event hand_discard_move = make_discard(pid, tile, false);
          moves.push_back(pon_move);
          moves.push_back(hand_discard_move);
          if (is_legal_pon_and_discard(game_record, game_state, moves)) {
            result.push_back(moves);
          }
        }
      }
    }
  }
  return result;
}

std::vector<Moves> get_legal_chii_discard_move(const Moves &game_record) {
  std::vector<Moves> result;
  const Event &action_json = game_record[game_record.size() - 1];
  if (action_json.type != EventType::DISCARD) {
    return result;
  }
  Game_State game_state = get_game_state(game_record);
  const int target = action_json.player;
  const int tile_target = action_json.tile;
  if (tile_target > 30) {
    return result;
  }
  const int pid = (target + 1) % 4;

  Event chii_move;
  if (tile_kind(tile_target) % 10 <= 7) {
    chii_move = make_chii_low_default(pid, target, tile_target);
    for (int tile = 1; tile < 38; tile++) {
      Moves moves;
      Event hand_discard_move = make_discard(pid, tile, false);
      moves.push_back(chii_move);
      moves.push_back(hand_discard_move);
      if (is_legal_chii_and_discard(game_record, game_state, moves)) {
        result.push_back(moves);
      }
    }
  }

  if (tile_kind(tile_target) % 10 == 3 || tile_kind(tile_target) % 10 == 4) {
    chii_move = make_chii_low_red(pid, target, tile_target);
    for (int tile = 1; tile < 38; tile++) {
      Moves moves;
      Event hand_discard_move = make_discard(pid, tile, false);
      moves.push_back(chii_move);
      moves.push_back(hand_discard_move);
      if (is_legal_chii_and_discard(game_record, game_state, moves)) {
        result.push_back(moves);
      }
    }
  }

  if (2 <= tile_kind(tile_target) % 10 && tile_kind(tile_target) % 10 <= 8) {
    chii_move = make_chii_middle_default(pid, target, tile_target);
    for (int tile = 1; tile < 38; tile++) {
      Moves moves;
      Event hand_discard_move = make_discard(pid, tile, false);
      moves.push_back(chii_move);
      moves.push_back(hand_discard_move);
      if (is_legal_chii_and_discard(game_record, game_state, moves)) {
        result.push_back(moves);
      }
    }
  }

  if (tile_kind(tile_target) % 10 == 4 || tile_kind(tile_target) % 10 == 6) {
    chii_move = make_chii_middle_red(pid, target, tile_target);
    for (int tile = 1; tile < 38; tile++) {
      Moves moves;
      Event hand_discard_move = make_discard(pid, tile, false);
      moves.push_back(chii_move);
      moves.push_back(hand_discard_move);
      if (is_legal_chii_and_discard(game_record, game_state, moves)) {
        result.push_back(moves);
      }
    }
  }

  if (3 <= tile_kind(tile_target) % 10) {
    chii_move = make_chii_high_default(pid, target, tile_target);
    for (int tile = 1; tile < 38; tile++) {
      Moves moves;
      Event hand_discard_move = make_discard(pid, tile, false);
      moves.push_back(chii_move);
      moves.push_back(hand_discard_move);
      if (is_legal_chii_and_discard(game_record, game_state, moves)) {
        result.push_back(moves);
      }
    }
  }

  if (tile_kind(tile_target) % 10 == 6 || tile_kind(tile_target) % 10 == 7) {
    chii_move = make_chii_high_red(pid, target, tile_target);
    for (int tile = 1; tile < 38; tile++) {
      Moves moves;
      Event hand_discard_move = make_discard(pid, tile, false);
      moves.push_back(chii_move);
      moves.push_back(hand_discard_move);
      if (is_legal_chii_and_discard(game_record, game_state, moves)) {
        result.push_back(moves);
      }
    }
  }
  return result;
}

std::vector<Moves> get_legal_open_kan_move(const Moves &game_record) {
  std::vector<Moves> result;
  const Event &action_json = game_record[game_record.size() - 1];
  if (action_json.type != EventType::DISCARD) {
    return result;
  }
  Game_State game_state = get_game_state(game_record);
  const int target = action_json.player;
  const int tile_target = action_json.tile;
  for (int pid = 0; pid < 4; pid++) {
    if (pid != target) {
      Event open_kan_move = make_open_kan_default(pid, target, tile_target);
      if (is_legal_open_kan(game_record, game_state, open_kan_move)) {
        Moves moves;
        moves.push_back(open_kan_move);
        result.push_back(moves);
      }
      if (tile_target % 10 == 5 && tile_target < 30) {
        open_kan_move = make_open_kan_red(pid, target, tile_target);
        if (is_legal_open_kan(game_record, game_state, open_kan_move)) {
          Moves moves;
          moves.push_back(open_kan_move);
          result.push_back(moves);
        }
      }
    }
  }
  return result;
}

std::vector<Moves> get_legal_concealed_kan_move(const Moves &game_record) {
  std::vector<Moves> result;
  const Event &action_json = game_record[game_record.size() - 1];
  if (action_json.type != EventType::DRAW) {
    return result;
  }

  Game_State game_state = get_game_state(game_record);
  const int actor = action_json.player;
  for (int tile = 1; tile < 38; tile++) {
    if (tile % 10 != 0) {
      Event concealed_kan_move = make_concealed_kan_default(actor, tile);
      if (is_legal_concealed_kan(game_record, game_state, concealed_kan_move)) {
        Moves moves;
        moves.push_back(concealed_kan_move);
        result.push_back(moves);
      }
    }
    if (tile % 10 == 5 && tile < 30) {
      Event concealed_kan_move = make_concealed_kan_red(actor, tile);
      if (is_legal_concealed_kan(game_record, game_state, concealed_kan_move)) {
        Moves moves;
        moves.push_back(concealed_kan_move);
        result.push_back(moves);
      }
    }
  }
  return result;
}

std::vector<Moves> get_legal_upgraded_kan_move(const Moves &game_record) {
  std::vector<Moves> result;
  const Event &action_json = game_record[game_record.size() - 1];
  if (action_json.type != EventType::DRAW) {
    return result;
  }

  Game_State game_state = get_game_state(game_record);
  const int actor = action_json.player;
  for (int tile = 1; tile < 38; tile++) {
    Event upgraded_kan_move = make_upgraded_kan_default(actor, tile);
    if (is_legal_upgraded_kan(game_record, game_state, upgraded_kan_move)) {
      Moves moves;
      moves.push_back(upgraded_kan_move);
      result.push_back(moves);
    }
    if (tile % 10 == 5 && tile < 30) {
      upgraded_kan_move = make_upgraded_kan_red(actor, tile);
      if (is_legal_upgraded_kan(game_record, game_state, upgraded_kan_move)) {
        Moves moves;
        moves.push_back(upgraded_kan_move);
        result.push_back(moves);
      }
    }
  }
  return result;
}

std::vector<Moves> get_legal_riichi_discard_move(const Moves &game_record) {
  std::vector<Moves> result;
  const Event &action_json = game_record[game_record.size() - 1];
  if (action_json.type != EventType::DRAW) {
    return result;
  }

  Game_State game_state = get_game_state(game_record);
  const int actor = action_json.player;
  const Event riichi_move = make_riichi(actor);
  Moves discard_from_draw_riichi;
  discard_from_draw_riichi.push_back(riichi_move);
  discard_from_draw_riichi.push_back(make_discard(actor, action_json.tile, true));
  if (is_legal_riichi_and_discard(game_record, game_state, discard_from_draw_riichi)) {
    result.push_back(discard_from_draw_riichi);
    // TODO: Reuse the shanten number from is_legal_riichi_and_discard and stop at two or more.
  }
  for (int tile = 1; tile < 38; tile++) {
    Moves moves;
    Event hand_discard_move = make_discard(actor, tile, false);
    moves.push_back(riichi_move);
    moves.push_back(hand_discard_move);
    if (is_legal_riichi_and_discard(game_record, game_state, moves)) {
      result.push_back(moves);
    }
  }
  return result;
}

std::vector<Moves> get_legal_tsumo_win_move(const Moves &game_record) {
  std::vector<Moves> result;
  const Event &action_json = game_record[game_record.size() - 1];
  if (action_json.type != EventType::DRAW) {
    return result;
  }
  Game_State game_state = get_game_state(game_record);
  const int actor = action_json.player;
  Event tsumo_win_move = make_win(actor, actor, action_json.tile);
  if (is_legal_win(game_record, game_state, tsumo_win_move)) {
    Moves moves;
    moves.push_back(tsumo_win_move);
    result.push_back(moves);
  }
  return result;
}

std::vector<Moves> get_legal_ron_move(const Moves &game_record) {
  std::vector<Moves> result;
  const Event &action_json = game_record[game_record.size() - 1];
  if (action_json.type != EventType::DISCARD && action_json.type != EventType::UPGRADED_KAN) {
    return result;
  }

  Game_State game_state = get_game_state(game_record);
  const int target = action_json.player;
  const int discard = action_json.tile;
  for (int pid = 0; pid < 4; pid++) {
    if (pid != target) {
      Moves moves;
      Event ron_move = make_win(pid, target, discard);
      if (is_legal_win(game_record, game_state, ron_move)) {
        moves.push_back(ron_move);
        result.push_back(moves);
      }
    }
  }
  return result;
}

std::array<std::vector<Moves>, 4> get_all_legal_moves(const Moves &game_record) {
  std::array<std::vector<Moves>, 4> all_legal_moves;
  std::vector<Moves> moves_vec = get_legal_discard_from_draw_move(game_record);
  for (int i = 0; i < moves_vec.size(); i++) {
    all_legal_moves[moves_vec[i][0].player].push_back(moves_vec[i]);
  }

  moves_vec = get_legal_hand_discard_move(game_record);
  for (int i = 0; i < moves_vec.size(); i++) {
    all_legal_moves[moves_vec[i][0].player].push_back(moves_vec[i]);
  }

  moves_vec = get_legal_pon_discard_move(game_record);
  for (int i = 0; i < moves_vec.size(); i++) {
    all_legal_moves[moves_vec[i][0].player].push_back(moves_vec[i]);
  }

  moves_vec = get_legal_chii_discard_move(game_record);
  for (int i = 0; i < moves_vec.size(); i++) {
    all_legal_moves[moves_vec[i][0].player].push_back(moves_vec[i]);
  }

  moves_vec = get_legal_open_kan_move(game_record);
  for (int i = 0; i < moves_vec.size(); i++) {
    all_legal_moves[moves_vec[i][0].player].push_back(moves_vec[i]);
  }

  moves_vec = get_legal_concealed_kan_move(game_record);
  for (int i = 0; i < moves_vec.size(); i++) {
    all_legal_moves[moves_vec[i][0].player].push_back(moves_vec[i]);
  }

  moves_vec = get_legal_upgraded_kan_move(game_record);
  for (int i = 0; i < moves_vec.size(); i++) {
    all_legal_moves[moves_vec[i][0].player].push_back(moves_vec[i]);
  }

  moves_vec = get_legal_riichi_discard_move(game_record);
  for (int i = 0; i < moves_vec.size(); i++) {
    all_legal_moves[moves_vec[i][0].player].push_back(moves_vec[i]);
  }

  moves_vec = get_legal_tsumo_win_move(game_record);
  for (int i = 0; i < moves_vec.size(); i++) {
    all_legal_moves[moves_vec[i][0].player].push_back(moves_vec[i]);
  }

  moves_vec = get_legal_ron_move(game_record);
  for (int i = 0; i < moves_vec.size(); i++) {
    all_legal_moves[moves_vec[i][0].player].push_back(moves_vec[i]);
  }

  if (game_record.back().type == EventType::DISCARD) {
    for (int pid = 0; pid < 4; pid++) {
      if (all_legal_moves[pid].size() > 0) {
        all_legal_moves[pid].push_back({make_none(pid)});
      }
    }
  }
  return all_legal_moves;
}

std::vector<Event> get_all_legal_single_action(const Moves &game_record) {
  std::vector<Event> ret;

  const Event &last_action = game_record[game_record.size() - 1];
  const EventType typ = last_action.type;
  if (typ == EventType::CHII || typ == EventType::PON || typ == EventType::RIICHI) {
    std::array<std::vector<Moves>, 4> all_legal_moves =
        get_all_legal_moves(Moves(game_record.begin(), game_record.end() - 1));
    for (int pid = 0; pid < 4; pid++) {
      for (Moves moves : all_legal_moves[pid]) {
        if (moves[0] == last_action) {
          ret.push_back(moves[1]);
        }
      }
    }
  } else {
    std::array<std::vector<Moves>, 4> all_legal_moves = get_all_legal_moves(game_record);
    for (int pid = 0; pid < 4; pid++) {
      for (Moves moves : all_legal_moves[pid]) {
        if (ret.size() == 0 || moves[0] != ret.back()) {
          ret.push_back(moves[0]);
        }
      }
    }
  }

  return ret;
}
