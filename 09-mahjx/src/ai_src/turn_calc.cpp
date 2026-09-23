#include "turn_calc.hpp"

#include "params.hpp"

std::array<std::array<float, 4>, 4> calc_turn_prob_end(const std::array<int, 4> &points,
                                                       const int origin_dealer) {
  std::array<std::array<float, 4>, 4> turn_prob;
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      turn_prob[i][j] = 0.0;
    }
  }
  std::vector<Player_Result> results;
  for (int j = 0; j < 4; j++) {
    results.push_back(Player_Result(j, points[j], (4 + j - origin_dealer) % 4));
  }
  std::sort(results.begin(), results.end());
  for (int j = 0; j < 4; j++) {
    turn_prob[results[j].pid][j] = 1.0;
  }
  return turn_prob;
}

std::array<int, 16> score_list_to_4onehot(const std::array<int, 4> &score) {
  std::vector<Player_Result> results;
  for (int i = 0; i < 4; i++) {
    results.push_back(Player_Result(i, score[i], i));
  }
  std::sort(results.begin(), results.end());

  std::array<int, 16> onehot;
  std::fill(onehot.begin(), onehot.end(), 0);
  for (int j = 0; j < 4; j++) {
    onehot[4 * j + results[j].pid] = 1;
  }
  return onehot;
}

std::array<float, 24> infer_game_result_prob(const std::array<int, 4> &score,
                                             const int ranking_model_round) {
  float w[96];
  const float *const params = RANK_PARA[ranking_model_round];
  for (int i = 0; i < 96; i++) w[i] = params[i];
  float x[4];
  x[0] = 1.0;
  for (int i = 1; i <= 3; i++) {
    x[i] = ((float)(score[0] - score[i])) / 10000.0;
  }
  float pk[24];
  MC_logistic(w, x, pk, 4, 24);

  std::array<float, 24> output;
  for (int i = 0; i < 24; i++) {
    output[i] = pk[i];
  }
  return output;
}

// Determine whether the game ends in the last round when the dealer wins or the round is drawn with
// the dealer in tenpai.
bool dealer_end_game(const std::array<int, 4> &points, const int dealer_id) {
  if (points[dealer_id] >= 30000) {
    for (int pid = 0; pid < 4; pid++) {
      if (pid == dealer_id) {
        continue;
      } else if (points[pid] >= points[dealer_id]) {
        return false;
      }
    }
    return true;
  } else {
    return false;
  }
}

// Determine whether the game ends in West-4 when the dealer wins or the round is drawn with the
// dealer in tenpai.
bool dealer_end_game_w4(const std::array<int, 4> &points, const int dealer_id) {
  for (int pid = 0; pid < 4; pid++) {
    if (pid == dealer_id) {
      continue;
    } else if (points[pid] >= points[dealer_id]) {
      return false;
    }
  }
  return true;
}

std::array<std::array<float, 4>, 4> calc_turn_prob(const int ranking_model_round,
                                                   const std::array<int, 4> &points,
                                                   const int dealer, const bool is_dealer_repeat,
                                                   const Tactics &tactics) {
  const int origin_dealer = (12 + dealer - ranking_model_round) % 4;
  if (is_dealer_repeat) {
    if ((ranking_model_round >= 7 && dealer_end_game(points, dealer)) ||
        (ranking_model_round == 11 && dealer_end_game_w4(points, dealer))) {
      return calc_turn_prob_end(points, origin_dealer);
    }
  }

  std::array<std::array<float, 4>, 4> turn_prob, turn_prob_tmp;
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      turn_prob[i][j] = 0.0;
      turn_prob_tmp[i][j] = 0.0;
    }
  }

  if (ranking_model_round < 8 || (ranking_model_round < 12 && points[0] < 30000 &&
                                  points[1] < 30000 && points[2] < 30000 && points[3] < 30000)) {
    std::array<int, 4> x;
    for (int i = 0; i < 4; i++) {
      x[i] = points[(origin_dealer + i) % 4];
    }

    const std::array<float, 24> pk = [&] {
      // TODO: Use round 5 or implement an East-South model when ranking_model_round is 4.
      return infer_game_result_prob(x, ranking_model_round);
    }();

    int turn[4];
    for (int i = 0; i < 24; i++) {
      for (int j = 0; j < 4; j++) {
        turn[j] = j;
      }
      for (int m = 0; m < 3; m++) {
        int k = (i % factorial(4 - m)) / factorial(3 - m);
        int tmp = turn[k + m];
        for (int n = 0; n < k; n++) {
          turn[k - n + m] = turn[k - n + m - 1];
        }
        turn[m] = tmp;
      }

      for (int j = 0; j < 4; j++) {
        for (int k = 0; k < 4; k++) {
          if (turn[k] == j) {
            turn_prob_tmp[j][k] += pk[i];
          }
        }
      }
    }
    for (int pid = 0; pid < 4; pid++) {
      for (int i = 0; i < 4; i++) {
        turn_prob[pid][i] = turn_prob_tmp[mod_pid(ranking_model_round, dealer, pid)][i];
      }
    }
    return turn_prob;
  } else {
    return calc_turn_prob_end(points, origin_dealer);
  }
}
