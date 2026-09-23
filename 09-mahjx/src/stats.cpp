#include "stats.hpp"

#include "share/record_io.hpp"

void show_stats(const std::string &dir_name) {
  int result_num[4][4] = {};
  int round_num = 0;
  int riichi_num[4] = {};
  int open_meld_num[4] = {};
  int open_meld_prob[4] = {};
  int has_open_meld[4] = {};
  int win_num[4] = {};
  int deal_in_num[4] = {};
  std::vector<std::string> files_path = get_files_path(dir_name);
  for (const std::string &path : files_path) {
    const std::string file_name = str_split(path, '/').back();
    if (file_name == "setup_match.txt" || !str_starts_with(file_name, "game_record_") ||
        file_name.size() < 4 || file_name.substr(file_name.size() - 4) != ".txt") {
      continue;
    }
    int origin_dealer = -1;
    bool round_stats_open = false;
    Event last_result;
    bool have_last_result = false;
    const auto close_round_stats = [&]() {
      if (!round_stats_open) return;
      for (int i = 0; i < 4; i++) {
        open_meld_prob[i] += has_open_meld[i];
      }
      round_stats_open = false;
    };
    const Moves events = load_game_record(path);
    for (const Event &event : events) {
      if (event.type == EventType::GAME) {
        origin_dealer = event.dealer;
      } else if (event.type == EventType::ROUND) {
        // Normally the result closes the prior round. Keep this fallback for
        // records where a round boundary is the first available delimiter.
        close_round_stats();
        round_num++;
        for (int i = 0; i < 4; i++) {
          has_open_meld[i] = 0;
        }
        round_stats_open = true;
      } else if (event.type == EventType::RIICHI) {
        riichi_num[event.player]++;
      } else if (event.type == EventType::CHII || event.type == EventType::PON) {
        open_meld_num[event.player]++;
        has_open_meld[event.player] = 1;
      } else if (event.type == EventType::WIN) {
        win_num[event.player]++;
        if (event.player != event.target) {
          deal_in_num[event.target]++;
        }
        last_result = event;
        have_last_result = true;
        close_round_stats();
      } else if (is_round_result(event.type)) {
        last_result = event;
        have_last_result = true;
        close_round_stats();
      } else if (event.type == EventType::END_GAME && have_last_result) {
        std::vector<Player_Result> results;
        for (int j = 0; j < 4; j++) {
          results.push_back(Player_Result(j, last_result.scores[j], (4 + j - origin_dealer) % 4));
        }
        std::sort(results.begin(), results.end());
        for (int j = 0; j < 4; j++) {
          result_num[results[j].pid][j]++;
        }
      }
    }
    close_round_stats();
  }
  std::cout << std::endl;
  for (int i = 0; i < 4; i++) {
    // Rough output.
    std::cout << result_num[i][0] << " " << result_num[i][1] << " " << result_num[i][2] << " "
              << result_num[i][3] << " ";
    double turn_average =
        result_num[i][0] + result_num[i][1] * 2.0 + result_num[i][2] * 3.0 + result_num[i][3] * 4.0;
    turn_average =
        turn_average / (result_num[i][0] + result_num[i][1] + result_num[i][2] + result_num[i][3]);
    std::cout << turn_average << " " << double(riichi_num[i]) / round_num << " "
              << double(open_meld_prob[i]) / round_num << " "
              << double(open_meld_num[i]) / round_num << " ";
    std::cout << double(win_num[i]) / round_num << " " << double(deal_in_num[i]) / round_num
              << std::endl;
  }
  std::cout << std::endl;
  // Clean output.
  const int game_num = result_num[0][0] + result_num[0][1] + result_num[0][2] + result_num[0][3];
  std::cout << "average_rank:";
  for (int pid = 0; pid < 4; pid++) {
    std::cout << " "
              << (result_num[pid][0] + result_num[pid][1] * 2.0 + result_num[pid][2] * 3.0 +
                  result_num[pid][3] * 4.0) /
                     game_num;
  }
  std::cout << std::endl;

  for (int i = 0; i < 4; i++) {
    std::cout << "rank" + std::to_string(i + 1) + "_prob:";
    for (int pid = 0; pid < 4; pid++) {
      std::cout << " " << float(result_num[pid][i]) / game_num;
    }
    std::cout << std::endl;
  }

  std::cout << "win_prob:";
  for (int pid = 0; pid < 4; pid++) {
    std::cout << " " << float(win_num[pid]) / round_num;
  }
  std::cout << std::endl;

  std::cout << "deal_in_prob:";
  for (int pid = 0; pid < 4; pid++) {
    std::cout << " " << float(deal_in_num[pid]) / round_num;
  }
  std::cout << std::endl;

  std::cout << "riichi_prob:";
  for (int pid = 0; pid < 4; pid++) {
    std::cout << " " << float(riichi_num[pid]) / round_num;
  }
  std::cout << std::endl;

  std::cout << "open_meld_prob:";
  for (int pid = 0; pid < 4; pid++) {
    std::cout << " " << float(open_meld_prob[pid]) / round_num;
  }
  std::cout << std::endl;

  std::cout << "open_meld_num:";
  for (int pid = 0; pid < 4; pid++) {
    std::cout << " " << float(open_meld_num[pid]) / round_num;
  }
  std::cout << std::endl;
}
