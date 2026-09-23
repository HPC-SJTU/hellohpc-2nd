#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>

#include "../analysis_io.hpp"
#include "../streaming_analyzer.hpp"

namespace {

void fail(const std::string &message) {
  std::cerr << "full analyze protocol test: " << message << std::endl;
  std::exit(1);
}

void check(const bool condition, const std::string &message) {
  if (!condition) fail(message);
}

size_t trace_index(const std::vector<std::string> &trace, const std::string &event) {
  for (size_t i = 0; i < trace.size(); ++i)
    if (trace[i] == event) return i;
  fail("missing trace event: " + event);
  return trace.size();
}

bool trace_contains(const std::vector<std::string> &trace, const std::string &event) {
  for (size_t i = 0; i < trace.size(); ++i)
    if (trace[i] == event) return true;
  return false;
}

class TracingOutputBuffer : public std::streambuf {
 public:
  TracingOutputBuffer(std::vector<std::string> *trace, const std::string &write_event,
                      const std::string &flush_event)
      : trace_(trace), write_event_(write_event), flush_event_(flush_event), wrote_(false) {}

  const std::string &text() const { return text_; }

 protected:
  virtual std::streamsize xsputn(const char *text, std::streamsize count) {
    note_write();
    text_.append(text, static_cast<size_t>(count));
    return count;
  }

  virtual int_type overflow(int_type value) {
    if (traits_type::eq_int_type(value, traits_type::eof())) return traits_type::not_eof(value);
    note_write();
    text_.push_back(traits_type::to_char_type(value));
    return value;
  }

  virtual int sync() {
    trace_->push_back(flush_event_);
    return 0;
  }

 private:
  void note_write() {
    if (!wrote_) {
      trace_->push_back(write_event_);
      wrote_ = true;
    }
  }

  std::vector<std::string> *trace_;
  std::string write_event_;
  std::string flush_event_;
  bool wrote_;
  std::string text_;
};

class PreloadedTracingInputBuffer : public std::streambuf {
 public:
  PreloadedTracingInputBuffer(const std::string &text, const size_t watched_offset,
                              std::vector<std::string> *trace)
      : text_(text), watched_offset_(watched_offset), trace_(trace), offset_(0), noted_(false) {}

 protected:
  virtual int_type underflow() {
    note_read();
    if (offset_ == text_.size()) return traits_type::eof();
    return traits_type::to_int_type(text_[offset_]);
  }

  virtual int_type uflow() {
    const int_type value = underflow();
    if (!traits_type::eq_int_type(value, traits_type::eof())) ++offset_;
    return value;
  }

 private:
  void note_read() {
    if (!noted_ && offset_ >= watched_offset_) {
      trace_->push_back("NEXT_EVENT_READ");
      noted_ = true;
    }
  }

  std::string text_;
  size_t watched_offset_;
  std::vector<std::string> *trace_;
  size_t offset_;
  bool noted_;
};

class TracingNumPut : public std::num_put<char> {
 public:
  explicit TracingNumPut(std::vector<std::string> *trace) : trace_(trace), noted_(false) {}

 protected:
  virtual iter_type do_put(iter_type out, std::ios_base &stream, char_type fill, long value) const {
    note_format();
    return std::num_put<char>::do_put(out, stream, fill, value);
  }

  virtual iter_type do_put(iter_type out, std::ios_base &stream, char_type fill,
                           unsigned long value) const {
    note_format();
    return std::num_put<char>::do_put(out, stream, fill, value);
  }

  virtual iter_type do_put(iter_type out, std::ios_base &stream, char_type fill,
                           double value) const {
    note_format();
    return std::num_put<char>::do_put(out, stream, fill, value);
  }

  virtual iter_type do_put(iter_type out, std::ios_base &stream, char_type fill,
                           long double value) const {
    note_format();
    return std::num_put<char>::do_put(out, stream, fill, value);
  }

 private:
  void note_format() const {
    if (!noted_ && trace_contains(*trace_, "MARKER_FLUSH")) {
      trace_->push_back("CANDIDATE_FORMAT");
      noted_ = true;
    }
  }

  std::vector<std::string> *trace_;
  mutable bool noted_;
};

class GlobalLocaleGuard {
 public:
  explicit GlobalLocaleGuard(const std::locale &replacement)
      : original_(std::locale::global(replacement)) {}
  ~GlobalLocaleGuard() { std::locale::global(original_); }

 private:
  std::locale original_;
};

void write_all(const int fd, const std::string &text) {
  size_t offset = 0;
  while (offset < text.size()) {
    const ssize_t written = write(fd, text.data() + offset, text.size() - offset);
    if (written < 0) {
      if (errno == EINTR) continue;
      fail(std::string("write failed: ") + std::strerror(errno));
    }
    offset += static_cast<size_t>(written);
  }
}

void append_available(const int fd, std::string *text) {
  char buffer[4096];
  for (;;) {
    const ssize_t count = read(fd, buffer, sizeof(buffer));
    if (count > 0) {
      text->append(buffer, static_cast<size_t>(count));
      continue;
    }
    if (count == 0 || errno == EAGAIN || errno == EWOULDBLOCK) return;
    if (errno == EINTR) continue;
    fail(std::string("read failed: ") + std::strerror(errno));
  }
}

void poll_until(const int output_fd, const int marker_fd, std::string *output, std::string *markers,
                const std::string &output_token, const std::string &marker_token) {
  for (int attempt = 0; attempt < 20; ++attempt) {
    append_available(output_fd, output);
    append_available(marker_fd, markers);
    const size_t output_at = output->find(output_token);
    const size_t marker_at = markers->find(marker_token);
    if (output_at != std::string::npos && marker_at != std::string::npos &&
        output->find('\n', output_at) != std::string::npos &&
        markers->find('\n', marker_at) != std::string::npos)
      return;
    struct pollfd fds[2] = {{output_fd, POLLIN, 0}, {marker_fd, POLLIN, 0}};
    const int ready = poll(fds, 2, 100);
    if (ready < 0 && errno != EINTR) fail(std::string("poll failed: ") + std::strerror(errno));
  }
  fail("timed out waiting for flushed protocol output\nstdout:\n" + *output + "stderr:\n" +
       *markers);
}

std::vector<std::string> fields(const std::string &line) {
  std::vector<std::string> result;
  size_t begin = 0;
  for (size_t end = 0; end <= line.size(); ++end) {
    if (end == line.size() || line[end] == '\t') {
      result.push_back(line.substr(begin, end - begin));
      begin = end + 1;
    }
  }
  return result;
}

std::string line_with_prefix(const std::string &text, const std::string &prefix,
                             const size_t occurrence = 0) {
  std::istringstream input(text);
  std::string line;
  size_t found = 0;
  while (std::getline(input, line)) {
    if (line.compare(0, prefix.size(), prefix) == 0 && found++ == occurrence) return line;
  }
  return std::string();
}

bool decimal(const std::string &text) {
  if (text.empty()) return false;
  char *end = NULL;
  errno = 0;
  std::strtod(text.c_str(), &end);
  return !errno && end != text.c_str() && *end == '\0';
}

void check_step(const std::string &markers, const size_t occurrence, const char *step,
                const char *event, const char *player) {
  const std::vector<std::string> value =
      fields(line_with_prefix(markers, "FULL_ANALYZE_STEP\t", occurrence));
  check(value.size() == 10, "FULL_ANALYZE_STEP must have all ten fields");
  check(value[0] == "FULL_ANALYZE_STEP" && value[1] == step && value[2] == "EVENT" &&
            value[3] == event && value[4] == "PLAYER" && value[5] == player &&
            value[6] == "ELAPSED_SECONDS" && decimal(value[7]) && value[8] == "STEP_SECONDS" &&
            decimal(value[9]),
        "FULL_ANALYZE_STEP field format is invalid");
}

Analysis_Position fixture_review(const Moves &, const int player, const int eid) {
  Analysis_Position position;
  position.player = player;
  position.trigger_eid = eid;
  Decision pass;
  pass.score = 1.0f;
  Event pass_action;
  pass_action.type = EventType::PASS;
  pass_action.player = player;
  pass.actions.push_back(pass_action);
  position.candidates.push_back(pass);
  if (eid == 2) {
    Decision discard;
    discard.score = 2.0f;
    Event discard_action;
    discard_action.type = EventType::DISCARD;
    discard_action.player = player;
    discard_action.tile = 1;
    discard_action.from_draw = true;
    discard.actions.push_back(discard_action);
    position.candidates.push_back(discard);
  }
  return position;
}

std::string initial_record() {
  std::ostringstream record;
  record << "GAME\t0\t0\t1\tE\t1\t0\t0\t0\t25000\t25000\t25000\t25000\n"
         << "ROUND\t1\t1m";
  for (int player = 0; player < 4; ++player) {
    record << "\nHAND\t1\t" << player << "\t13";
    for (int i = 0; i < 13; ++i) record << (player == 0 ? "\t1m" : "\t?");
  }
  record << "\nACTION\t2\tDRAW\t0\t1m\n";
  return record.str();
}

void test_protocol_phase_order_and_read_barrier() {
  std::vector<std::string> trace;
  const std::string first = initial_record();
  const std::string second =
      "ACTION\t3\tDISCARD\t0\t1m\t1\n"
      "DRAWN_EXHAUSTIVE\t4\t0\t0\t0\t0\t25000\t25000\t25000\t25000\n"
      "HAND\t4\t0\t0\nHAND\t4\t1\t0\nHAND\t4\t2\t0\nHAND\t4\t3\t0\n";
  PreloadedTracingInputBuffer input_buffer(first + second, first.size(), &trace);
  TracingOutputBuffer output_buffer(&trace, "CANDIDATE_WRITE", "CANDIDATE_FLUSH");
  TracingOutputBuffer marker_buffer(&trace, "MARKER_WRITE", "MARKER_FLUSH");
  std::istream input(&input_buffer);
  std::ostream output(&output_buffer);
  std::ostream markers(&marker_buffer);
  const std::locale traced_locale(std::locale::classic(), new TracingNumPut(&trace));
  GlobalLocaleGuard locale_guard(traced_locale);
  markers.imbue(traced_locale);

  run_full_analyze_protocol(input, output, markers, 1,
                            [&](const Moves &record, const int player, const int eid) {
                              check(player == 0, "only known player reviewed");
                              for (int opponent = 1; opponent < 4; ++opponent)
                                for (int tile : record[1].hands[opponent])
                                  check(tile == -1, "review preserves unknown hands");
                              Analysis_Position position = fixture_review(record, player, eid);
                              trace.push_back("REVIEW_COMPLETE");
                              return position;
                            });

  const size_t review = trace_index(trace, "REVIEW_COMPLETE");
  const size_t marker_write = trace_index(trace, "MARKER_WRITE");
  const size_t marker_flush = trace_index(trace, "MARKER_FLUSH");
  const size_t candidate_format = trace_index(trace, "CANDIDATE_FORMAT");
  const size_t candidate_write = trace_index(trace, "CANDIDATE_WRITE");
  const size_t candidate_flush = trace_index(trace, "CANDIDATE_FLUSH");
  const size_t next_event_read = trace_index(trace, "NEXT_EVENT_READ");
  check(review < marker_write, "review completion must precede marker write");
  check(marker_write < marker_flush, "marker write must precede marker flush");
  check(marker_flush < candidate_format, "marker flush must precede candidate formatting");
  check(candidate_format < candidate_write, "candidate formatting must precede candidate write");
  check(candidate_write < candidate_flush, "candidate write must precede candidate flush");
  check(candidate_flush < next_event_read,
        "protocol read the next preloaded Event before flushing the first candidate block");
  check(output_buffer.text().find("POS\t2\t0\t2\n") == 0,
        "instrumented protocol did not emit the first candidate block");
}

void test_error_serialization() {
  Analysis_Position unmatched;
  Event discard;
  discard.type = EventType::DISCARD;
  discard.player = 2;
  discard.tile = 15;
  discard.from_draw = true;
  unmatched.actual.push_back(discard);
  check(
      write_position_result(unmatched) == "ACTUAL\t1\nACTION\t0\tDISCARD\t2\t5p\t1\nERR_UNMATCHED",
      "ERR_UNMATCHED reference serialization changed");

  Analysis_Position unobserved = unmatched;
  check(write_position_result(unobserved, true) == "ACTUAL\t0\nERR_UNOBSERVED",
        "ERR_UNOBSERVED reference serialization changed");
}

}  // namespace

int run_fixture_child() {
  try {
    run_full_analyze_protocol(std::cin, std::cout, std::cerr, 3, fixture_review);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "fixture error: " << error.what() << std::endl;
    return 1;
  }
}

void test_full_analyze_protocol() {
  int input_pipe[2];
  int output_pipe[2];
  int marker_pipe[2];
  if (pipe(input_pipe) || pipe(output_pipe) || pipe(marker_pipe)) fail("pipe creation failed");
  const pid_t child = fork();
  if (child < 0) fail("fork failed");
  if (child == 0) {
    dup2(input_pipe[0], STDIN_FILENO);
    dup2(output_pipe[1], STDOUT_FILENO);
    dup2(marker_pipe[1], STDERR_FILENO);
    close(input_pipe[0]);
    close(input_pipe[1]);
    close(output_pipe[0]);
    close(output_pipe[1]);
    close(marker_pipe[0]);
    close(marker_pipe[1]);
    _exit(run_fixture_child());
  }
  close(input_pipe[0]);
  close(output_pipe[1]);
  close(marker_pipe[1]);
  fcntl(output_pipe[0], F_SETFL, fcntl(output_pipe[0], F_GETFL) | O_NONBLOCK);
  fcntl(marker_pipe[0], F_SETFL, fcntl(marker_pipe[0], F_GETFL) | O_NONBLOCK);

  std::string output;
  std::string markers;
  write_all(input_pipe[1], initial_record());
  poll_until(output_pipe[0], marker_pipe[0], &output, &markers, "ACTION\t0\tDISCARD\t0\t1m\t1\n",
             "FULL_ANALYZE_STEP\t1\t");
  check(output.find("POS\t2\t0\t2\n") == 0, "POS field format is invalid");
  check(output.find("CAND\t0\t1\t-\t-\t-\t1\nACTION\t0\tPASS\t0\n") != std::string::npos,
        "first CAND/ACTION field format is invalid");
  check(output.find("CAND\t1\t2\t-\t-\t-\t1\nACTION\t0\tDISCARD\t0\t1m\t1\n") != std::string::npos,
        "second CAND/ACTION field format is invalid");
  check(output.find("ACTUAL") == std::string::npos,
        "ACTUAL was emitted before the actual action arrived");
  check_step(markers, 0, "1", "2", "0");

  struct pollfd quiet[2] = {{output_pipe[0], POLLIN, 0}, {marker_pipe[0], POLLIN, 0}};
  const int unexpected = poll(quiet, 2, 100);
  if (unexpected < 0 && errno != EINTR) fail("quiet poll failed");
  append_available(output_pipe[0], &output);
  append_available(marker_pipe[0], &markers);
  check(output.find("ACTUAL") == std::string::npos, "ACTUAL appeared while stdin remained open");

  write_all(input_pipe[1], "ACTION\t3\tDISCARD\t0\t1m\t1\n");
  poll_until(output_pipe[0], marker_pipe[0], &output, &markers, "ERR_OK\t1\t2\t2\t0\n",
             "FULL_ANALYZE_STEP\t2\t");
  check(output.find("ACTUAL\t1\nACTION\t0\tDISCARD\t0\t1m\t1\n"
                    "ERR_OK\t1\t2\t2\t0\n") != std::string::npos,
        "ACTUAL/ERR fields were not flushed in full");
  check_step(markers, 1, "2", "3", "1");
  check(output.find("POS\t3\t1") == std::string::npos,
        "a position with fewer than two candidates emitted POS");

  write_all(input_pipe[1],
            "DRAWN_EXHAUSTIVE\t4\t0\t0\t0\t0\t25000\t25000\t25000\t25000\n"
            "HAND\t4\t0\t0\nHAND\t4\t1\t0\nHAND\t4\t2\t0\nHAND\t4\t3\t0\n");
  close(input_pipe[1]);
  int status = 0;
  if (waitpid(child, &status, 0) < 0) fail("waitpid failed");
  close(output_pipe[0]);
  close(marker_pipe[0]);
  check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "fixture process failed");
}

int main() {
  test_protocol_phase_order_and_read_barrier();
  test_error_serialization();
  test_full_analyze_protocol();
  return 0;
}
