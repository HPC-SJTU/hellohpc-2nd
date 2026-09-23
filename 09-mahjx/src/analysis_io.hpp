#pragma once

#include <string>
#include <vector>

#include "analyze.hpp"

std::string canonical_action_text(const Moves &actions);
std::string write_action_records(const Moves &actions);
std::string write_candidate_actions(const Moves &actions, int index);
std::string write_legal_decisions(const std::vector<Moves> &decisions);
std::string write_position_candidates(const Analysis_Position &position);
std::string write_position_result(const Analysis_Position &position, bool unobserved = false);
