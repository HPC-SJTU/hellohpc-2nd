#include "win_points.hpp"

int ron_win(const int han, const int fu, const bool is_dealer) {
  if (han == 0) {
    return 0;
  } else {
    if (is_dealer) {
      if (han >= 13) {
        return 48000;
      } else if (han >= 11) {
        return 36000;
      } else if (han >= 8) {
        return 24000;
      } else if (han >= 6) {
        return 18000;
      } else if (han >= 5) {
        return 12000;
      } else if (han == 4) {
        if (fu == 25) {
          return 9600;
        } else if (fu <= 30) {
          return 11600;
        } else {
          return 12000;
        }
      } else if (han == 3) {
        if (fu == 25) {
          return 4800;
        } else if (fu <= 30) {
          return 5800;
        } else if (fu <= 40) {
          return 7700;
        } else if (fu <= 50) {
          return 9600;
        } else if (fu <= 60) {
          return 11600;
        } else {
          return 12000;
        }
      } else if (han == 2) {
        if (fu == 25) {
          return 2400;
        } else if (fu <= 30) {
          return 2900;
        } else if (fu <= 40) {
          return 3900;
        } else if (fu <= 50) {
          return 4800;
        } else if (fu <= 60) {
          return 5800;
        } else if (fu <= 70) {
          return 6800;
        } else if (fu <= 80) {
          return 7700;
        } else if (fu <= 90) {
          return 8700;
        } else if (fu <= 100) {
          return 9600;
        } else if (fu <= 110) {
          return 10600;
        }
      } else if (han == 1) {
        if (fu <= 30) {
          return 1500;
        } else if (fu <= 40) {
          return 2000;
        } else if (fu <= 50) {
          return 2400;
        } else if (fu <= 60) {
          return 2900;
        } else if (fu <= 70) {
          return 3400;
        } else if (fu <= 80) {
          return 3900;
        } else if (fu <= 90) {
          return 4400;
        } else if (fu <= 100) {
          return 4800;
        } else if (fu <= 110) {
          return 5300;
        }
      }
    } else {
      if (han >= 13) {
        return 32000;
      } else if (han >= 11) {
        return 24000;
      } else if (han >= 8) {
        return 16000;
      } else if (han >= 6) {
        return 12000;
      } else if (han >= 5) {
        return 8000;
      } else if (han == 4) {
        if (fu == 25) {
          return 6400;
        } else if (fu <= 30) {
          return 7700;
        } else {
          return 8000;
        }
      } else if (han == 3) {
        if (fu == 25) {
          return 3200;
        } else if (fu <= 30) {
          return 3900;
        } else if (fu <= 40) {
          return 5200;
        } else if (fu <= 50) {
          return 6400;
        } else if (fu <= 60) {
          return 7700;
        } else {
          return 8000;
        }
      } else if (han == 2) {
        if (fu == 25) {
          return 1600;
        } else if (fu <= 30) {
          return 2000;
        } else if (fu <= 40) {
          return 2600;
        } else if (fu <= 50) {
          return 3200;
        } else if (fu <= 60) {
          return 3900;
        } else if (fu <= 70) {
          return 4500;
        } else if (fu <= 80) {
          return 5200;
        } else if (fu <= 90) {
          return 5800;
        } else if (fu <= 100) {
          return 6400;
        } else if (fu <= 110) {
          return 7100;
        }
      } else if (han == 1) {
        if (fu <= 30) {
          return 1000;
        } else if (fu <= 40) {
          return 1300;
        } else if (fu <= 50) {
          return 1600;
        } else if (fu <= 60) {
          return 2000;
        } else if (fu <= 70) {
          return 2300;
        } else if (fu <= 80) {
          return 2600;
        } else if (fu <= 90) {
          return 2900;
        } else if (fu <= 100) {
          return 3200;
        } else if (fu <= 110) {
          return 3600;
        }
      }
    }
  }
  assert_with_out(false, "ron_win error");
  return 0;
}

int tsumo_win_loss(const int han, const int fu, const bool is_dealer) {
  if (han == 0) {
    return 0;
  } else {
    if (is_dealer) {
      if (han >= 13) {
        return 16000;
      } else if (han >= 11) {
        return 12000;
      } else if (han >= 8) {
        return 8000;
      } else if (han >= 6) {
        return 6000;
      } else if (han >= 5) {
        return 4000;
      } else if (han == 4) {
        if (fu == 20) {
          return 2600;
        } else if (fu == 25) {
          return 3200;
        } else if (fu <= 30) {
          return 3900;
        } else {
          return 4000;
        }
      } else if (han == 3) {
        if (fu == 20) {
          return 1300;
        } else if (fu == 25) {
          return 1600;
        } else if (fu <= 30) {
          return 2000;
        } else if (fu <= 40) {
          return 2600;
        } else if (fu <= 50) {
          return 3200;
        } else if (fu <= 60) {
          return 3900;
        } else {
          return 4000;
        }
      } else if (han == 2) {
        if (fu == 20) {
          return 700;
        } else if (fu == 25) {
          return 800;
        } else if (fu <= 30) {
          return 1000;
        } else if (fu <= 40) {
          return 1300;
        } else if (fu <= 50) {
          return 1600;
        } else if (fu <= 60) {
          return 2000;
        } else if (fu <= 70) {
          return 2300;
        } else if (fu <= 80) {
          return 2600;
        } else if (fu <= 90) {
          return 2900;
        } else if (fu <= 100) {
          return 3200;
        } else if (fu <= 110) {
          return 3600;
        }
      } else if (han == 1) {
        if (fu <= 30) {
          return 500;
        } else if (fu <= 40) {
          return 700;
        } else if (fu <= 50) {
          return 800;
        } else if (fu <= 60) {
          return 1000;
        } else if (fu <= 70) {
          return 1200;
        } else if (fu <= 80) {
          return 1300;
        } else if (fu <= 90) {
          return 1500;
        } else if (fu <= 100) {
          return 1600;
        } else if (fu <= 110) {
          return 1800;
        }
      }
    } else {
      if (han >= 13) {
        return 8000;
      } else if (han >= 11) {
        return 6000;
      } else if (han >= 8) {
        return 4000;
      } else if (han >= 6) {
        return 3000;
      } else if (han >= 5) {
        return 2000;
      } else if (han == 4) {
        if (fu == 20) {
          return 1300;
        } else if (fu == 25) {
          return 1600;
        } else if (fu <= 30) {
          return 2000;
        } else {
          return 2000;
        }
      } else if (han == 3) {
        if (fu == 20) {
          return 700;
        } else if (fu == 25) {
          return 800;
        } else if (fu <= 30) {
          return 1000;
        } else if (fu <= 40) {
          return 1300;
        } else if (fu <= 50) {
          return 1600;
        } else if (fu <= 60) {
          return 2000;
        } else {
          return 2000;
        }
      } else if (han == 2) {
        if (fu == 20) {
          return 400;
        } else if (fu <= 30) {
          return 500;
        } else if (fu <= 40) {
          return 700;
        } else if (fu <= 50) {
          return 800;
        } else if (fu <= 60) {
          return 1000;
        } else if (fu <= 70) {
          return 1200;
        } else if (fu <= 80) {
          return 1300;
        } else if (fu <= 90) {
          return 1500;
        } else if (fu <= 100) {
          return 1600;
        } else if (fu <= 110) {
          return 1800;
        }
      } else if (han == 1) {
        if (fu <= 30) {
          return 300;
        } else if (fu <= 40) {
          return 400;
        } else if (fu <= 50) {
          return 400;
        } else if (fu <= 60) {
          return 500;
        } else if (fu <= 70) {
          return 600;
        } else if (fu <= 80) {
          return 700;
        } else if (fu <= 90) {
          return 800;
        } else if (fu <= 100) {
          return 800;
        } else {
          return 0;  // This win is impossible.
        }
      }
    }
  }
  assert_with_out(false, "tsumo_win_loss error");
  return 0;
}

int tsumo_win(const int han, const int fu, const bool is_dealer) {
  if (is_dealer) {
    return tsumo_win_loss(han, fu, is_dealer) * 3;
  } else {
    return tsumo_win_loss(han, fu, true) + tsumo_win_loss(han, fu, false) * 2;
  }
}

std::array<int, 4> points_move_win(const int who, const int from_who, const int han, const int fu,
                                   const int dealer, const int repeat_multiplier, const int stick) {
  std::array<int, 4> points_move;
  for (int pid = 0; pid < 4; pid++) {
    points_move[pid] = 0;
  }

  if (dealer == who) {
    if (who == from_who) {
      for (int pid = 0; pid < 4; pid++) {
        if (pid == who) {
          points_move[pid] += tsumo_win(han, fu, true) + repeat_multiplier * 300 + stick * 1000;
        } else {
          points_move[pid] -= tsumo_win_loss(han, fu, true) + repeat_multiplier * 100;
        }
      }
    } else {
      points_move[who] += ron_win(han, fu, true) + repeat_multiplier * 300 + stick * 1000;
      points_move[from_who] -= ron_win(han, fu, true) + repeat_multiplier * 300;
    }
  } else {
    if (who == from_who) {
      for (int pid = 0; pid < 4; pid++) {
        if (pid == who) {
          points_move[pid] += tsumo_win(han, fu, false) + repeat_multiplier * 300 + stick * 1000;
        } else if (pid == dealer) {
          points_move[pid] -= tsumo_win_loss(han, fu, true) + repeat_multiplier * 100;
        } else {
          points_move[pid] -= tsumo_win_loss(han, fu, false) + repeat_multiplier * 100;
        }
      }
    } else {
      points_move[who] += ron_win(han, fu, false) + repeat_multiplier * 300 + stick * 1000;
      points_move[from_who] -= ron_win(han, fu, false) + repeat_multiplier * 300;
    }
  }
  return points_move;
}

std::array<int, 4> points_move_drawn_round(const std::array<bool, 4> &is_tenpai) {
  int tenpai_num = 0;
  std::array<int, 4> points_move;
  for (int pid = 0; pid < 4; pid++) {
    points_move[pid] = 0;
    tenpai_num += is_tenpai[pid] ? 1 : 0;
  }

  if (tenpai_num == 1) {
    for (int pid = 0; pid < 4; pid++) {
      points_move[pid] = is_tenpai[pid] ? 3000 : -1000;
    }
  } else if (tenpai_num == 2) {
    for (int pid = 0; pid < 4; pid++) {
      points_move[pid] = is_tenpai[pid] ? 1500 : -1500;
    }
  } else if (tenpai_num == 3) {
    for (int pid = 0; pid < 4; pid++) {
      points_move[pid] = is_tenpai[pid] ? 1000 : -3000;
    }
  }
  return points_move;
}
