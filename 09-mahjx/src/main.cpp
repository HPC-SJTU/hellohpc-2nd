#include "main.hpp"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <map>
#include <sstream>
#include <stdexcept>

namespace {

int parse_integer_argument(const char *text, const char *name) {
  errno = 0;
  char *end = NULL;
  const long value = std::strtol(text, &end, 10);
  if (errno || end == text || *end != '\0' || value < INT_MIN || value > INT_MAX)
    throw std::invalid_argument(std::string("invalid ") + name);
  return static_cast<int>(value);
}

void copy_file(const std::string &source, const std::string &destination) {
  std::ifstream input(source.c_str(), std::ios::binary);
  if (!input) throw std::runtime_error("cannot open setup file");
  std::ofstream output(destination.c_str(), std::ios::binary);
  if (!output) throw std::runtime_error("cannot copy setup file");
  output << input.rdbuf();
}

std::vector<Moves> legal_decisions(const Moves &game_record) {
  if (game_record.empty()) throw std::runtime_error("empty game record");
  std::vector<Moves> decisions;
  const Event &last = game_record.back();
  if (last.type == EventType::CHII || last.type == EventType::PON ||
      last.type == EventType::RIICHI) {
    const Moves prefix(game_record.begin(), game_record.end() - 1);
    const std::array<std::vector<Moves>, 4> all = get_all_legal_moves(prefix);
    for (int player = 0; player < 4; ++player) {
      for (const Moves &moves : all[player]) {
        if (!moves.empty() && moves[0] == last)
          decisions.push_back(Moves(moves.begin() + 1, moves.end()));
      }
    }
  } else {
    const std::array<std::vector<Moves>, 4> all = get_all_legal_moves(game_record);
    for (int player = 0; player < 4; ++player)
      decisions.insert(decisions.end(), all[player].begin(), all[player].end());
  }
  std::map<std::string, Moves> unique;
  for (const Moves &moves : decisions) unique[canonical_action_text(moves)] = moves;
  decisions.clear();
  for (const std::pair<const std::string, Moves> &item : unique) decisions.push_back(item.second);
  return decisions;
}

void run_full_analyze(const std::string &setup_file, int mask) {
  if (mask < 1 || mask > 15) throw std::invalid_argument("player mask must be 1..15");
  const Match_Config config = load_match_config(setup_file);
  set_tactics(config.tactics);
  run_full_analyze_protocol(std::cin, std::cout, std::cerr, mask, analyze_position);
}

void usage() {
  std::cout << "Usage:\n"
            << "  system test <seed_begin> <seed_end>\n"
            << "  system check <setup_match_file> <game_record_file>\n"
            << "  system legal_action <game_record_file>\n"
            << "  system legal_action_log_all <game_record_file>\n"
            << "  system full_analyze <setup_match_file> <mask>  (game record from stdin)\n"
            << "  system stats <directory>\n"
            << "  system para_check\n";
}

}  // namespace

int main(int argc, char *argv[]) {
  try {
    if (argc == 4 && std::string(argv[1]) == "test") {
      const Match_Config config = load_match_config("setup_match.txt");
      make_dir(config.result_dir);
      copy_file("setup_match.txt", config.result_dir + "/setup_match.txt");
      set_tactics(config.tactics);
      const int seed_begin = parse_integer_argument(argv[2], "seed_begin");
      const int seed_end = parse_integer_argument(argv[3], "seed_end");
      if (seed_end < seed_begin) throw std::invalid_argument("seed_end precedes seed_begin");
      for (int seed = seed_begin; seed < seed_end; ++seed) {
        for (const Game_Origin &game_origin : config.games) {
          seed_rng(seed);
          Moves game_record;
          std::vector<int> wall;
          game_loop(wall, game_record, game_origin, -1);
          moves_to_file(game_record, config.result_dir + "/game_record_" + std::to_string(seed) +
                                         "_" + std::to_string(game_origin.dealer) + ".txt");
        }
      }
      return 0;
    }
    if (argc == 4 && std::string(argv[1]) == "check") {
      const Match_Config config = load_match_config(argv[2]);
      set_tactics(config.tactics);
      Moves game_record = load_game_record(argv[3]);
      std::vector<int> wall;
      proceed_game(wall, game_record, -1, Event());
      return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "legal_action") {
      const Moves game_record = load_game_record(argv[2]);
      std::cout << write_legal_decisions(legal_decisions(game_record)) << std::endl;
      return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "legal_action_log_all") {
      const Moves game_record = load_game_record(argv[2]);
      Moves prefix;
      for (const Event &event : game_record) {
        prefix.push_back(event);
        const std::vector<Moves> decisions = legal_decisions(prefix);
        std::cout << "LEGAL_POS\t" << event.eid << '\t' << decisions.size() << '\n';
        const std::string body = write_legal_decisions(decisions);
        const size_t first_newline = body.find('\n');
        if (first_newline != std::string::npos) std::cout << body.substr(first_newline + 1) << '\n';
      }
      return 0;
    }
    if (argc == 4 && std::string(argv[1]) == "full_analyze") {
      run_full_analyze(argv[2], parse_integer_argument(argv[3], "mask"));
      return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "stats") {
      show_stats(argv[2]);
      return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "para_check") {
#pragma omp parallel
      {
        printf("parallel_thread=%d\n", omp_get_thread_num());
      }
      return 0;
    }
    usage();
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "error: " << error.what() << std::endl;
    return 1;
  }
}
