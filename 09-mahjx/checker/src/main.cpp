#include <cstdio>
#include <fstream>
#include <string_view>

#include "checker.hpp"

namespace {

auto print_maximum(std::string_view name, const full_analyze_checker::ErrorMaximum &maximum)
    -> void {
  if (maximum.count == 0) {
    std::printf("MAX_MIN_ERROR\t%.*s\tCOUNT\t0\tVALUE\t-\n", static_cast<int>(name.size()),
                name.data());
    return;
  }
  std::printf("MAX_MIN_ERROR\t%.*s\tCOUNT\t%zu\tVALUE\t%.17Lg\n", static_cast<int>(name.size()),
              name.data(), maximum.count, maximum.maximum);
}

}  // namespace

auto main(int argc, char *argv[]) -> int {
  if (argc != 3) {
    std::fprintf(stderr, "usage: checker <reference-output> <submission-output>\n");
    return 2;
  }

  std::ifstream reference_file(argv[1]);
  if (!reference_file) {
    std::fprintf(stderr, "reference: cannot open the file\n");
    return 2;
  }
  auto reference = full_analyze_checker::parse_output(reference_file);
  if (!reference) {
    std::fprintf(stderr, "reference: %s\n", reference.error().c_str());
    return 2;
  }

  std::ifstream submission_file(argv[2]);
  if (!submission_file) {
    std::fprintf(stderr, "submission: cannot open the file\n");
    return 1;
  }
  auto submission = full_analyze_checker::parse_output(submission_file);
  if (!submission) {
    std::fprintf(stderr, "submission: %s\n", submission.error().c_str());
    return 1;
  }

  auto statistics = full_analyze_checker::compare_outputs(*reference, *submission);
  if (!statistics) {
    std::fprintf(stderr, "submission: %s\n", statistics.error().c_str());
    return 1;
  }

  print_maximum("CANDIDATE_SCORE", statistics->candidate_score);
  print_maximum("POINTS_AFTER", statistics->points_after);
  print_maximum("DEAL_IN_PROBABILITY", statistics->deal_in_probability);
  print_maximum("DEAL_IN_WEIGHTED_UTILITY", statistics->deal_in_weighted_utility);
  print_maximum("ERROR_BEST_SCORE", statistics->error_best_score);
  print_maximum("ERROR_ACTUAL_SCORE", statistics->error_actual_score);
  print_maximum("ERROR", statistics->error);
  return 0;
}
