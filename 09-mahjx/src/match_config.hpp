#pragma once

#include "ai_src/tactics.hpp"
#include "game_manager.hpp"

struct Match_Config {
  std::string result_dir;
  std::vector<Game_Origin> games;
  std::array<Tactics, 4> tactics;
};

Match_Config load_match_config(const std::string &file_name);
