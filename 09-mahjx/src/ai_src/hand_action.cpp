#include "hand_action.hpp"

extern const bool console_out;

bool is_pon(Action_Type action_type) {
  return (action_type == AT_PON || action_type == AT_PON_WITH_RED);
}

bool is_open_kan(Action_Type action_type) { return action_type == AT_OPEN_KAN; }

bool is_concealed_kan(Action_Type action_type) {
  return (action_type == AT_CONCEALED_KAN || action_type == AT_CONCEALED_KAN_AND_RIICHI_DECLARE);
}

bool is_upgraded_kan(Action_Type action_type) { return action_type == AT_UPGRADED_KAN; }

bool is_chii_low(Action_Type action_type) {
  return (action_type == AT_CHII_LOW || action_type == AT_CHII_LOW_WITH_RED);
}

bool is_chii_middle(Action_Type action_type) {
  return (action_type == AT_CHII_MIDDLE || action_type == AT_CHII_MIDDLE_WITH_RED);
}

bool is_chii_high(Action_Type action_type) {
  return (action_type == AT_CHII_HIGH || action_type == AT_CHII_HIGH_WITH_RED);
}

bool is_chii(Action_Type action_type) {
  return (is_chii_low(action_type) || is_chii_middle(action_type) || is_chii_high(action_type));
}

bool is_riichi_declare(Action_Type action_type) {
  return (action_type == AT_RIICHI_DECLARE || action_type == AT_CONCEALED_KAN_AND_RIICHI_DECLARE);
}

Tsumo_Edge::Tsumo_Edge() {}

Tsumo_Edge::Tsumo_Edge(const int sg, const uint8_t tile_in_value, const uint8_t tile_out_value,
                       const int dg) {
  src_group = sg;
  tile_in = tile_in_value;
  tile_out = tile_out_value;
  dst_group = dg;
}

Hand_Action::Hand_Action() {}

void Hand_Action::print_info() const {
  if (!console_out) return;
  std::cout << "tile:" << int(tile) << "  action_type:" << action_type
            << "  tile_out:" << int(tile_out) << std::endl;
  std::cout << "dst_group:" << int(dst_group) << " dst_group_sub:" << int(dst_group_sub)
            << std::endl;
}
