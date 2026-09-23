#include "response_priority.hpp"

int first_win_responder(const int target, const std::array<Moves, 4> &candidate_moves) {
  for (int pid_add = 1; pid_add <= 3; pid_add++) {
    const int actor = (target + pid_add) % 4;
    if (!candidate_moves[actor].empty() && candidate_moves[actor][0].type == EventType::WIN) {
      return actor;
    }
  }
  return -1;
}
