#pragma once

#include "../share/include.hpp"

enum Action_Type : char {
  AT_TSUMO = 0,
  AT_DISCARD = -1,
  AT_RIICHI_DECLARE = 19,
  AT_RIICHI_ACCEPTED = 20,  // If no one wins with ron on the declared tile, the order is: riichi
                            // declaration, discard, riichi acceptance, next action. If someone wins
                            // with ron, the round ends after: riichi declaration, discard, ron.
  AT_CHII_LOW = 11,     // Call chii on a tile with a small number. Example: expose 2m and 3m, then
                        // call chii on 1m.
  AT_CHII_MIDDLE = 12,  // Call chii on a tile with a middle number.
  AT_CHII_HIGH = 13,    // Call chii on a tile with a large number.
  AT_PON = 2,
  AT_OPEN_KAN = 3,
  AT_CONCEALED_KAN = 4,
  AT_UPGRADED_KAN = 5,
  AT_CHII_LOW_WITH_RED =
      111,  // Chii that contains a red five in the claimed tile or the exposed tiles.
  AT_CHII_MIDDLE_WITH_RED = 112,
  AT_CHII_HIGH_WITH_RED = 113,
  AT_PON_WITH_RED = 102,
  AT_OPEN_KAN_WITH_RED = 103,  // Whether an open kan contains a red tile is clear from the rules
                               // and the tile kind. This value can be unnecessary.
  AT_TSUMO_WIN = 50,
  AT_RON_WIN = 51,
  AT_CONCEALED_KAN_AND_RIICHI_DECLARE =
      52,  // A dead wall draw comes before a concealed kan and a riichi declaration. This grouping
           // is unnatural, but we define it because we sometimes want its expected value.
  AT_OPEN_MELD_PASS = 53,
  AT_NINE_TERMINALS = 54,
  AT_NULL = -100,
};

bool is_pon(Action_Type action_type);
bool is_open_kan(Action_Type action_type);
bool is_concealed_kan(Action_Type action_type);
bool is_upgraded_kan(Action_Type action_type);
bool is_chii_low(Action_Type action_type);
bool is_chii_middle(Action_Type action_type);
bool is_chii_high(Action_Type action_type);
bool is_chii(Action_Type action_type);
bool is_riichi_declare(Action_Type action_type);

class Tsumo_Edge {
 public:
  Tsumo_Edge();
  Tsumo_Edge(const int sg, const uint8_t tile_in_value, const uint8_t tile_out_value, const int dg);

  uint8_t tile_in;
  uint8_t tile_out;
  int src_group;
  int dst_group;
};

class Hand_Action {
 public:
  Hand_Action();

  uint8_t tile;
  uint8_t tile_out;
  Action_Type action_type;
  uint8_t dst_group_sub;
  // This variable uses an Action_Type without a red tile even for an open meld with a red tile.
  // The expected values are AT_TSUMO, AT_PON, AT_CHII_LOW, AT_CHII_MIDDLE, AT_CHII_HIGH,
  // AT_OPEN_KAN, AT_CONCEALED_KAN, AT_UPGRADED_KAN. We can tell the riichi state from dst_group and
  // dst_group_sub, so action_type does not distinguish it. We can probably tell whether the open
  // meld contains a red tile from dst_group and dst_group_sub.
  int dst_group;

  void print_info() const;
};
