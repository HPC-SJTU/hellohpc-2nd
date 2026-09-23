#include "checker.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>

namespace {

const std::string REFERENCE_OUTPUT =
    "POS\t10\t0\t2\n"
    "CAND\t0\t1\t0.5\t0.2\t-1\t1\n"
    "ACTION\t0\tDISCARD\t0\t1m\t1\n"
    "CAND\t1\t0.5\t-\t-\t-\t1\n"
    "ACTION\t0\tDISCARD\t0\t2m\t0\n"
    "ACTUAL\t1\n"
    "ACTION\t0\tDISCARD\t0\t1m\t1\n"
    "ERR_OK\t0\t1\t1\t0\n";

const std::string REORDERED_OUTPUT =
    "POS\t10\t0\t2\n"
    "CAND\t0\t0.5\t-\t-\t-\t1\n"
    "ACTION\t0\tDISCARD\t0\t2m\t0\n"
    "CAND\t1\t1\t0.5\t0.2\t-1\t1\n"
    "ACTION\t0\tDISCARD\t0\t1m\t1\n"
    "ACTUAL\t1\n"
    "ACTION\t0\tDISCARD\t0\t1m\t1\n"
    "ERR_OK\t1\t1\t1\t0\n";

constexpr std::array<std::string_view, 13> ACTION_RECORDS = {
    "ACTION\t0\tDRAW\t0\t1m",
    "ACTION\t0\tDISCARD\t0\t1m\t1",
    "ACTION\t0\tRIICHI\t0",
    "ACTION\t0\tRIICHI_ACCEPTED\t0",
    "ACTION\t0\tDORA\t1m",
    "ACTION\t0\tCHII\t0\t1\t3m\t2m\t4m",
    "ACTION\t0\tPON\t0\t1\t3m\t3m\t3m",
    "ACTION\t0\tOPEN_KAN\t0\t1\t3m\t3m\t3m\t3m",
    "ACTION\t0\tCONCEALED_KAN\t0\t3m\t3m\t3m\t3m",
    "ACTION\t0\tUPGRADED_KAN\t0\t3m\t3m\t3m\t3m",
    "ACTION\t0\tWIN\t0\t1\t3m",
    "ACTION\t0\tNINE_TERMINALS\t0",
    "ACTION\t0\tPASS\t0",
};

auto parse(const std::string &text) -> full_analyze_checker::AnalyzeOutput {
  std::istringstream input(text);
  auto output = full_analyze_checker::parse_output(input);
  if (!output) {
    std::fprintf(stderr, "%s\n", output.error().c_str());
    std::exit(1);
  }
  return *output;
}

auto expect_match(const std::string &reference, const std::string &submission) -> void {
  const auto result = full_analyze_checker::compare_outputs(parse(reference), parse(submission));
  if (!result) {
    std::fprintf(stderr, "unexpected mismatch: %s\n", result.error().c_str());
    std::exit(1);
  }
}

auto expect_mismatch(const std::string &reference, const std::string &submission) -> void {
  const auto result = full_analyze_checker::compare_outputs(parse(reference), parse(submission));
  if (result) {
    std::fprintf(stderr, "expected a mismatch\n");
    std::exit(1);
  }
}

auto expect_parse_error(const std::string &text) -> void {
  std::istringstream input(text);
  if (full_analyze_checker::parse_output(input)) {
    std::fprintf(stderr, "expected a parse error\n");
    std::exit(1);
  }
}

auto replace_once(std::string text, const std::string &source, const std::string &target)
    -> std::string {
  const std::size_t position = text.find(source);
  if (position == std::string::npos) std::exit(1);
  text.replace(position, source.size(), target);
  return text;
}

auto output_with_score(std::string_view score) -> std::string {
  std::string output =
      replace_once(REFERENCE_OUTPUT, "CAND\t0\t1\t", "CAND\t0\t" + std::string(score) + "\t");
  return replace_once(output, "ERR_OK\t0\t1\t1\t0",
                      "ERR_OK\t0\t" + std::string(score) + "\t" + std::string(score) + "\t0");
}

auto unmatched_output(std::string_view action_record) -> std::string {
  return "POS\t1\t0\t1\nCAND\t0\t0\t-\t-\t-\t1\n" + std::string(action_record) +
         "\nACTUAL\t0\nERR_UNMATCHED\n";
}

auto unobserved_output() -> std::string {
  return "POS\t1\t1\t1\n"
         "CAND\t0\t0\t-\t-\t-\t1\n"
         "ACTION\t0\tPASS\t1\n"
         "ACTUAL\t0\n"
         "ERR_UNOBSERVED\n";
}

auto two_phase_output() -> std::string {
  return "POS\t20\t0\t1\n"
         "CAND\t0\t1\t-\t-\t-\t1\n"
         "ACTION\t0\tPASS\t0\n"
         "POS\t20\t2\t1\n"
         "CAND\t0\t0\t-\t-\t-\t1\n"
         "ACTION\t0\tPASS\t2\n"
         "ACTUAL\t1\n"
         "ACTION\t0\tPASS\t0\n"
         "ERR_OK\t0\t1\t1\t0\n"
         "ACTUAL\t0\n"
         "ERR_UNOBSERVED\n";
}

}  // namespace

auto main() -> int {
  expect_match(REFERENCE_OUTPUT, REFERENCE_OUTPUT);
  expect_match(REFERENCE_OUTPUT, REORDERED_OUTPUT);
  expect_match(REFERENCE_OUTPUT, replace_once(REFERENCE_OUTPUT, "\t0.5\t0.2\t-1\t1\n",
                                              "\t0.500019\t0.200003\t-1.000019\t1\n"));
  expect_match(REFERENCE_OUTPUT, output_with_score("1.000019"));
  expect_match(output_with_score("100"), output_with_score("100.0019"));

  expect_match(REFERENCE_OUTPUT, output_with_score("1.000021"));
  expect_match(output_with_score("100"), output_with_score("100.0021"));
  expect_match(unobserved_output(),
               replace_once(unobserved_output(), "CAND\t0\t0\t", "CAND\t0\t0.000099999\t"));
  expect_mismatch(unobserved_output(),
                  replace_once(unobserved_output(), "CAND\t0\t0\t", "CAND\t0\t0.000100001\t"));
  expect_match(REFERENCE_OUTPUT, output_with_score("1.0001"));
  expect_mismatch(REFERENCE_OUTPUT, output_with_score("1.00011"));
  expect_match(output_with_score("100"), output_with_score("100.01"));
  expect_mismatch(output_with_score("100"), output_with_score("100.011"));
  expect_mismatch(REFERENCE_OUTPUT,
                  replace_once(REFERENCE_OUTPUT, "\t0.5\t0.2\t-1\t1\n", "\t0.5\t-\t-1\t1\n"));
  expect_mismatch(REFERENCE_OUTPUT,
                  replace_once(REFERENCE_OUTPUT, "DISCARD\t0\t2m\t0", "DISCARD\t0\t3m\t0"));

  for (const std::string_view action_record : ACTION_RECORDS) {
    const std::string output = unmatched_output(action_record);
    expect_match(output, output);
  }
  expect_match(unobserved_output(), unobserved_output());
  expect_mismatch(unobserved_output(),
                  replace_once(unobserved_output(), "ERR_UNOBSERVED", "ERR_UNMATCHED"));
  expect_parse_error(
      replace_once(unobserved_output(), "ACTUAL\t0\n", "ACTUAL\t1\nACTION\t0\tPASS\t1\n"));
  expect_match(two_phase_output(), two_phase_output());
  expect_match(
      replace_once(REFERENCE_OUTPUT, "DISCARD\t0\t1m\t1\nERR_OK", "DISCARD\t0\t1m\t0\nERR_OK"),
      replace_once(REFERENCE_OUTPUT, "DISCARD\t0\t1m\t1\nERR_OK", "DISCARD\t0\t1m\t0\nERR_OK"));
  expect_parse_error("ACTUAL\t0\nERR_UNOBSERVED\n");
  expect_parse_error(two_phase_output().substr(0, two_phase_output().find("ACTUAL\t1\n")));
  expect_parse_error(replace_once(two_phase_output(), "ACTUAL\t0\nERR_UNOBSERVED\n",
                                  "POS\t21\t1\t1\nCAND\t0\t0\t-\t-\t-\t1\n"
                                  "ACTION\t0\tPASS\t1\nACTUAL\t0\nERR_UNOBSERVED\n"));
  expect_parse_error(replace_once(two_phase_output(),
                                  "ACTUAL\t1\nACTION\t0\tPASS\t0\nERR_OK\t0\t1\t1\t0\n"
                                  "ACTUAL\t0\nERR_UNOBSERVED\n",
                                  "ACTUAL\t0\nERR_UNOBSERVED\nACTUAL\t1\n"
                                  "ACTION\t0\tPASS\t0\nERR_OK\t0\t1\t1\t0\n"));
  expect_parse_error(two_phase_output() + "ACTUAL\t0\nERR_UNOBSERVED\n");
  expect_parse_error(two_phase_output() +
                     "POS\t20\t0\t1\nCAND\t0\t0\t-\t-\t-\t1\n"
                     "ACTION\t0\tPASS\t0\nACTUAL\t0\nERR_UNOBSERVED\n");

  expect_parse_error(replace_once(REFERENCE_OUTPUT, "CAND\t0\t1\t", "CAND\t0\tnan\t"));
  expect_parse_error(replace_once(REFERENCE_OUTPUT, "CAND\t0\t1\t", "CAND\t0\tinf\t"));
  expect_parse_error(replace_once(REFERENCE_OUTPUT, "ERR_OK\t0\t1\t1\t0", "ERR_OK\t1\t1\t1\t0"));
  expect_parse_error(replace_once(REFERENCE_OUTPUT, "POS\t10\t0\t2", "POS\t10\t0\t2147483647"));
  expect_parse_error(replace_once(REFERENCE_OUTPUT, "CAND\t0\t1\t0.5\t0.2\t-1\t1",
                                  "CAND\t0\t1\t0.5\t0.2\t-1\t2147483647"));
  expect_parse_error(replace_once(REFERENCE_OUTPUT, "POS\t10\t0\t2", "POS\t10\t4\t2"));
  expect_parse_error(replace_once(REFERENCE_OUTPUT, "DISCARD\t0\t2m\t0", "DISCARD\t0\t0m\t0"));
  expect_parse_error(REFERENCE_OUTPUT.substr(0, REFERENCE_OUTPUT.size() - 1));
  expect_parse_error(REFERENCE_OUTPUT + "GARBAGE\n");

  return 0;
}
