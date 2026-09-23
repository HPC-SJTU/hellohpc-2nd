#include "riichi_rules.hpp"

namespace {

int normalized_tile(const int tile) { return tile % 10 == 0 ? tile - 5 : tile; }

bool is_tile_kind(const int tile) { return 1 <= tile && tile < 38 && tile % 10 != 0; }

bool complete_melds(Tile_Array &hand, const int melds_left, const int forbidden_triplet) {
  int tile = 1;
  while (tile < 38 && hand[tile] == 0) tile++;
  if (tile == 38) return melds_left == 0;
  if (melds_left == 0) return false;

  if (tile != forbidden_triplet && hand[tile] >= 3) {
    hand[tile] -= 3;
    if (complete_melds(hand, melds_left - 1, forbidden_triplet)) {
      hand[tile] += 3;
      return true;
    }
    hand[tile] += 3;
  }

  if (tile < 30 && tile % 10 <= 7 && hand[tile + 1] > 0 && hand[tile + 2] > 0) {
    hand[tile]--;
    hand[tile + 1]--;
    hand[tile + 2]--;
    if (complete_melds(hand, melds_left - 1, forbidden_triplet)) {
      hand[tile]++;
      hand[tile + 1]++;
      hand[tile + 2]++;
      return true;
    }
    hand[tile]++;
    hand[tile + 1]++;
    hand[tile + 2]++;
  }
  return false;
}

bool is_normal_complete(const Tile_Array &hand, const int fixed_melds,
                        const int forbidden_triplet = 0) {
  const int melds_left = 4 - fixed_melds;
  if (melds_left < 0) return false;

  int tile_count = 0;
  for (const int count : hand) tile_count += count;
  if (tile_count != 3 * melds_left + 2) return false;

  for (int pair_tile = 1; pair_tile < 38; pair_tile++) {
    if (hand[pair_tile] < 2) continue;
    Tile_Array work = hand;
    work[pair_tile] -= 2;
    if (complete_melds(work, melds_left, forbidden_triplet)) return true;
  }
  return false;
}

bool is_seven_pairs_complete(const Tile_Array &hand, const int fixed_melds) {
  if (fixed_melds != 0) return false;
  int pair_count = 0;
  for (int tile = 1; tile < 38; tile++) {
    if (!is_tile_kind(tile)) continue;
    if (hand[tile] == 2) {
      pair_count++;
    } else if (hand[tile] != 0) {
      return false;
    }
  }
  return pair_count == 7;
}

bool is_thirteen_orphans_complete(const Tile_Array &hand, const int fixed_melds) {
  if (fixed_melds != 0) return false;
  const int terminals_and_honors[] = {1, 9, 11, 19, 21, 29, 31, 32, 33, 34, 35, 36, 37};
  int pair_count = 0;
  int tile_count = 0;
  for (int tile = 1; tile < 38; tile++) {
    tile_count += hand[tile];
    bool required = false;
    for (const int required_tile : terminals_and_honors) {
      if (tile == required_tile) required = true;
    }
    if (!required && hand[tile] != 0) return false;
  }
  if (tile_count != 14) return false;
  for (const int tile : terminals_and_honors) {
    if (hand[tile] == 0 || hand[tile] > 2) return false;
    if (hand[tile] == 2) pair_count++;
  }
  return pair_count == 1;
}

bool is_complete(const Tile_Array &hand, const int fixed_melds) {
  return is_normal_complete(hand, fixed_melds) || is_seven_pairs_complete(hand, fixed_melds) ||
         is_thirteen_orphans_complete(hand, fixed_melds);
}

Tile_Array owned_tiles(const Tile_Array &hand, const Open_Meld_Vector &open_meld) {
  Tile_Array owned = hand;
  for (const Open_Meld_Elem &meld : open_meld) {
    if (meld.type != FT_CONCEALED_KAN) owned[normalized_tile(meld.tile)]++;
    for (const int tile : meld.consumed) owned[normalized_tile(tile)]++;
  }
  return owned;
}

bool has_wait(const std::array<bool, 38> &waits) {
  for (const bool wait : waits) {
    if (wait) return true;
  }
  return false;
}

std::array<bool, 38> waiting_tiles(const Tile_Array &hand, const Open_Meld_Vector &open_meld) {
  std::array<bool, 38> waits = {};
  const Tile_Array owned = owned_tiles(hand, open_meld);
  for (int tile = 1; tile < 38; tile++) {
    if (!is_tile_kind(tile) || owned[tile] >= 4) continue;
    Tile_Array complete_hand = hand;
    complete_hand[tile]++;
    waits[tile] = is_complete(complete_hand, open_meld.size());
  }
  return waits;
}

bool has_alternative_structure(const Tile_Array &hand, const int fixed_melds, const int kan_tile) {
  for (int wait_tile = 1; wait_tile < 38; wait_tile++) {
    if (!is_tile_kind(wait_tile)) continue;
    Tile_Array complete_hand = hand;
    complete_hand[wait_tile]++;
    if (is_normal_complete(complete_hand, fixed_melds) &&
        is_normal_complete(complete_hand, fixed_melds, kan_tile)) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool completing_tile_exists(const Tile_Array &hand, const Open_Meld_Vector &open_meld,
                            const int completing_tile) {
  const int kind = normalized_tile(completing_tile);
  return is_tile_kind(kind) && owned_tiles(hand, open_meld)[kind] < 4;
}

bool is_legal_concealed_kan_after_riichi(const Tile_Array &hand_before_draw,
                                         const Open_Meld_Vector &open_meld, const int drawn_tile) {
  const int kan_tile = normalized_tile(drawn_tile);
  Tile_Array hand = {};
  for (int tile = 1; tile < 38; tile++) {
    const int kind = normalized_tile(tile);
    if (is_tile_kind(kind)) hand[kind] += hand_before_draw[tile];
  }
  if (!is_tile_kind(kan_tile) || hand[kan_tile] != 3) return false;
  if (has_alternative_structure(hand, open_meld.size(), kan_tile)) return false;

  const std::array<bool, 38> waits_before = waiting_tiles(hand, open_meld);
  if (!has_wait(waits_before)) return false;

  Tile_Array hand_after_kan = hand;
  hand_after_kan[kan_tile] -= 3;
  Open_Meld_Vector open_meld_after_kan = open_meld;
  Open_Meld_Elem kan;
  kan.type = FT_CONCEALED_KAN;
  kan.consumed.assign(4, kan_tile);
  open_meld_after_kan.push_back(kan);
  return waits_before == waiting_tiles(hand_after_kan, open_meld_after_kan);
}
