#include "maimoe/simai.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <numeric>
#include <sstream>
#include <tuple>
#include <utility>

namespace maimoe {
namespace {

struct PositionedChar {
    char value = 0;
    SourceLocation location{};
};

struct SourceKey {
    bool touch = false;
    std::uint8_t index = 0;

    auto operator<=>(const SourceKey&) const = default;
};

struct SourceState {
    bool have_tick = false;
    std::uint32_t tick = 0;
    std::uint32_t active_end = 0;
};

void add_candidate(ParsedChart& chart, SemanticError candidate) {
    const auto existing = std::find_if(
        chart.semantic_candidates.begin(), chart.semantic_candidates.end(),
        [&candidate](const SemanticError& error) { return error.code == candidate.code; });
    if (existing == chart.semantic_candidates.end()) {
        chart.semantic_candidates.push_back(candidate);
        return;
    }
    if (std::tuple(candidate.location.byte_offset, candidate.detail) <
        std::tuple(existing->location.byte_offset, existing->detail)) {
        *existing = candidate;
    }
}

struct BodyParser {
    ParsedChart& chart;
    const std::vector<PositionedChar>& body;
    std::uint64_t tick = 0;
    std::uint32_t division = 4;
    std::uint32_t bpm_milli = 0;
    std::uint32_t parsed_events = 0;
    bool initial_bpm_control = true;
    bool initial_division_control = true;
    bool conventional_division_pending = false;
    bool ended = false;
    std::array<SourceState, 8> button_sources{};
    std::array<SourceState, kTouchSensorCount> touch_sources{};

    void add_error(SemanticErrorCode code, SourceLocation location, std::uint32_t detail) {
        add_candidate(chart, SemanticError{code, location, detail});
    }

    void add_warning(WarningCode code, SourceLocation location) {
        chart.warnings.push_back(Warning{code, 1, location});
    }
};

[[nodiscard]] SourceLocation location_at(const std::vector<PositionedChar>& body,
                                         std::size_t index) noexcept {
    if (body.empty()) {
        return SourceLocation{};
    }
    return body[std::min(index, body.size() - 1U)].location;
}

[[nodiscard]] std::uint32_t saturated_u32(std::uint64_t value) noexcept {
    return value > std::numeric_limits<std::uint32_t>::max()
               ? std::numeric_limits<std::uint32_t>::max()
               : static_cast<std::uint32_t>(value);
}

template <typename UInt>
[[nodiscard]] UInt parse_uint(std::string_view text, SourceLocation location,
                              std::string_view what) {
    if (text.empty() || (text.size() > 1U && text.front() == '0')) {
        throw FormatError(std::string(what) + " is not a canonical unsigned decimal",
                          location.line, location.column);
    }
    UInt value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw FormatError(std::string(what) + " is malformed or out of range",
                          location.line, location.column);
    }
    return value;
}

[[nodiscard]] std::uint32_t parse_bpm_milli(std::string_view text, SourceLocation location) {
    const std::size_t dot = text.find('.');
    if (dot != std::string_view::npos && text.find('.', dot + 1U) != std::string_view::npos) {
        throw FormatError("BPM contains more than one decimal point", location.line,
                          location.column);
    }
    const std::string_view whole_text = text.substr(0, dot);
    const std::string_view fraction = dot == std::string_view::npos
                                          ? std::string_view{}
                                          : text.substr(dot + 1U);
    if (whole_text.empty() || (whole_text.size() > 1U && whole_text.front() == '0') ||
        (dot != std::string_view::npos && (fraction.empty() || fraction.size() > 3U))) {
        throw FormatError("BPM is not a canonical decimal", location.line, location.column);
    }
    const std::uint32_t whole = parse_uint<std::uint32_t>(whole_text, location, "BPM");
    std::uint32_t fractional = 0;
    if (!fraction.empty()) {
        if (fraction.back() == '0') {
            throw FormatError("BPM fractional digits have a trailing zero", location.line,
                              location.column);
        }
        for (const char digit : fraction) {
            if (digit < '0' || digit > '9') {
                throw FormatError("BPM contains a non-decimal character", location.line,
                                  location.column);
            }
            fractional = fractional * 10U + static_cast<std::uint32_t>(digit - '0');
        }
        if (fraction.size() == 1U) {
            fractional *= 100U;
        } else if (fraction.size() == 2U) {
            fractional *= 10U;
        }
    }
    if (whole > (std::numeric_limits<std::uint32_t>::max() - fractional) / 1000U) {
        throw FormatError("BPM is out of the milli-BPM lexical range", location.line,
                          location.column);
    }
    return whole * 1000U + fractional;
}

[[nodiscard]] std::string bpm_text(std::uint32_t milli) {
    std::string result = std::to_string(milli / 1000U);
    const std::uint32_t fraction = milli % 1000U;
    if (fraction != 0U) {
        std::string digits = std::to_string(1000U + fraction).substr(1U);
        while (digits.back() == '0') {
            digits.pop_back();
        }
        result.push_back('.');
        result += digits;
    }
    return result;
}

[[nodiscard]] std::uint8_t touch_sensor(std::string_view text, SourceLocation location) {
    if (text == "C") {
        return 16U;
    }
    if (text.size() != 2U || text[1] < '1' || text[1] > '8') {
        throw FormatError("touch sensor must be A1..A8, B1..B8, C, D1..D8, or E1..E8",
                          location.line, location.column);
    }
    const std::uint8_t ring_index = static_cast<std::uint8_t>(text[1] - '1');
    switch (text[0]) {
    case 'A':
        return ring_index;
    case 'B':
        return static_cast<std::uint8_t>(8U + ring_index);
    case 'D':
        return static_cast<std::uint8_t>(17U + ring_index);
    case 'E':
        return static_cast<std::uint8_t>(25U + ring_index);
    default:
        throw FormatError("unsupported touch sensor ring", location.line, location.column);
    }
}

[[nodiscard]] std::string touch_name(std::uint8_t sensor) {
    if (sensor < 8U) {
        return std::string{"A"} + static_cast<char>('1' + sensor);
    }
    if (sensor < 16U) {
        return std::string{"B"} + static_cast<char>('1' + sensor - 8U);
    }
    if (sensor == 16U) {
        return "C";
    }
    if (sensor < 25U) {
        return std::string{"D"} + static_cast<char>('1' + sensor - 17U);
    }
    if (sensor < kTouchSensorCount) {
        return std::string{"E"} + static_cast<char>('1' + sensor - 25U);
    }
    throw FormatError("touch sensor index is outside 0..32");
}

[[nodiscard]] SourceKey event_source(const Event& event) noexcept {
    if (event.type == EventType::TouchTap || event.type == EventType::TouchHold) {
        return SourceKey{true, event.touch_sensor};
    }
    return SourceKey{false, event.lane};
}

[[nodiscard]] std::uint32_t source_detail(SourceKey source) noexcept {
    return source.touch ? 0x100U + source.index : source.index;
}

[[nodiscard]] std::string canonical_event(const Event& event) {
    std::ostringstream out;
    out << event.tick << ' ';
    switch (event.type) {
    case EventType::Tap:
        out << "TAP B" << static_cast<unsigned>(event.lane + 1U) << " 128";
        break;
    case EventType::Break:
        out << "BREAK B" << static_cast<unsigned>(event.lane + 1U) << " 255 3";
        break;
    case EventType::Hold:
        out << "HOLD B" << static_cast<unsigned>(event.lane + 1U) << ' ' << event.end_tick
            << " 128";
        break;
    case EventType::Slide:
        out << "SLIDE B" << static_cast<unsigned>(event.lane + 1U) << ' '
            << event.end_tick << " B" << static_cast<unsigned>(event.target_lane + 1U)
            << " 0";
        break;
    case EventType::TouchTap:
        out << "TOUCH " << touch_name(event.touch_sensor) << " 192";
        break;
    case EventType::TouchHold:
        out << "TOUCH_HOLD " << touch_name(event.touch_sensor) << ' ' << event.end_tick
            << " 192";
        break;
    }
    return out.str();
}

[[nodiscard]] std::uint64_t parse_duration(BodyParser& parser, std::string_view token,
                                           std::size_t token_offset) {
    const SourceLocation location = location_at(parser.body, token_offset);
    const std::size_t colon = token.find(':');
    if (colon == std::string_view::npos || token.find(':', colon + 1U) != std::string_view::npos) {
        throw FormatError("duration must be one canonical D:N pair", location.line,
                          location.column);
    }
    const std::uint32_t denominator = parse_uint<std::uint32_t>(
        token.substr(0, colon), location, "duration denominator");
    const std::uint32_t numerator = parse_uint<std::uint32_t>(
        token.substr(colon + 1U), location, "duration numerator");
    if (denominator == 0U || numerator == 0U) {
        throw FormatError("duration components must be nonzero", location.line, location.column);
    }
    if (std::gcd(denominator, numerator) != 1U) {
        throw FormatError("duration D:N must be reduced", location.line, location.column);
    }
    const std::uint64_t scaled = static_cast<std::uint64_t>(kTicksPerWhole) * numerator;
    if (scaled % denominator != 0U) {
        throw FormatError("duration is not exact on the 384-tick grid", location.line,
                          location.column);
    }
    return scaled / denominator;
}

void increment_count(ParsedChart& chart, EventType type) {
    switch (type) {
    case EventType::Tap:
        ++chart.counts.tap;
        break;
    case EventType::Break:
        ++chart.counts.break_count;
        break;
    case EventType::Hold:
        ++chart.counts.hold;
        break;
    case EventType::Slide:
        ++chart.counts.slide;
        break;
    case EventType::TouchTap:
    case EventType::TouchHold:
        ++chart.counts.touch;
        break;
    }
    ++chart.counts.total;
}

void parse_note(BodyParser& parser, std::string_view note, std::size_t note_offset) {
    const SourceLocation source = location_at(parser.body, note_offset);
    if (note.empty()) {
        throw FormatError("empty note in explicit chord", source.line, source.column);
    }

    Event event;
    event.tick = saturated_u32(parser.tick);
    event.location = source;
    const bool button = note.front() >= '1' && note.front() <= '8';
    std::size_t sensor_length = 0;
    if (button) {
        event.lane = static_cast<std::uint8_t>(note.front() - '1');
        sensor_length = 1;
    } else if (note.front() == 'C') {
        event.touch_sensor = touch_sensor(note.substr(0, 1), source);
        sensor_length = 1;
    } else if (note.front() == 'A' || note.front() == 'B' || note.front() == 'D' ||
               note.front() == 'E') {
        if (note.size() < 2U) {
            throw FormatError("truncated touch sensor", source.line, source.column);
        }
        event.touch_sensor = touch_sensor(note.substr(0, 2), source);
        sensor_length = 2;
    } else {
        throw FormatError("unsupported note syntax", source.line, source.column);
    }

    if (note.size() == sensor_length) {
        event.type = button ? EventType::Tap : EventType::TouchTap;
        event.strength = button ? 128U : 192U;
    } else if (button && note.size() == 2U && note[1] == 'b') {
        event.type = EventType::Break;
        event.strength = 255U;
    } else {
        const std::size_t open = note.find('[', sensor_length);
        if (open == std::string_view::npos || note.back() != ']' ||
            note.find('[', open + 1U) != std::string_view::npos ||
            note.find(']', open + 1U) != note.size() - 1U) {
            throw FormatError("timed note requires exactly one [D:N] suffix", source.line,
                              source.column);
        }
        const std::string_view head = note.substr(sensor_length, open - sensor_length);
        const std::uint64_t duration = parse_duration(
            parser, note.substr(open + 1U, note.size() - open - 2U), note_offset + open + 1U);
        const std::uint64_t end_tick = parser.tick + duration;
        event.end_tick = saturated_u32(end_tick);
        parser.chart.max_tick = std::max(parser.chart.max_tick, event.end_tick);
        if (end_tick > kMaxTimelineTick) {
            parser.add_error(SemanticErrorCode::TimelineRange, source,
                             saturated_u32(end_tick));
        }
        if (head == "h") {
            event.type = button ? EventType::Hold : EventType::TouchHold;
            event.strength = button ? 128U : 192U;
        } else if (button && head.size() == 2U && head.front() == '-' &&
                   head.back() >= '1' && head.back() <= '8') {
            event.type = EventType::Slide;
            event.target_lane = static_cast<std::uint8_t>(head.back() - '1');
            if (event.target_lane == event.lane) {
                parser.add_error(SemanticErrorCode::SlideSelf, source,
                                 static_cast<std::uint32_t>(event.lane + 1U));
            }
        } else {
            throw FormatError(button ? "unsupported button suffix"
                                     : "touch notes support only tap and h[D:N]",
                              source.line, source.column + sensor_length);
        }
    }

    if (parser.tick > kMaxTimelineTick) {
        parser.add_error(SemanticErrorCode::TimelineRange, source,
                         saturated_u32(parser.tick));
    }
    const SourceKey key = event_source(event);
    SourceState& source_state = key.touch ? parser.touch_sources[key.index]
                                          : parser.button_sources[key.index];
    if (source_state.have_tick && source_state.tick == event.tick) {
        parser.add_error(SemanticErrorCode::DuplicateSource, source, source_detail(key));
    } else if (source_state.active_end > event.tick) {
        parser.add_error(SemanticErrorCode::ActiveOverlap, source, source_state.active_end);
    }
    source_state.have_tick = true;
    source_state.tick = event.tick;
    if (event.type == EventType::Hold || event.type == EventType::Slide ||
        event.type == EventType::TouchHold) {
        source_state.active_end = std::max(source_state.active_end, event.end_tick);
    }
    increment_count(parser.chart, event.type);
    ++parser.parsed_events;
    if (parser.parsed_events == kMaxEvents + 1U) {
        parser.add_error(SemanticErrorCode::EventLimit, source, parser.parsed_events);
    }
    if (parser.parsed_events <= kMaxEvents) {
        event.canonical = canonical_event(event);
        parser.chart.events.push_back(std::move(event));
    }
}

void parse_controls(BodyParser& parser, std::string_view cell, std::size_t cell_offset,
                    std::size_t& cursor) {
    while (cursor < cell.size() && (cell[cursor] == '(' || cell[cursor] == '{')) {
        const char opening = cell[cursor];
        const char closing = opening == '(' ? ')' : '}';
        const std::size_t close = cell.find(closing, cursor + 1U);
        const SourceLocation location = location_at(parser.body, cell_offset + cursor);
        if (close == std::string_view::npos) {
            throw FormatError("unterminated timing control", location.line, location.column);
        }
        const std::string_view value = cell.substr(cursor + 1U, close - cursor - 1U);
        if (opening == '(') {
            const std::uint32_t bpm = parse_bpm_milli(value, location);
            const bool conventional = parser.initial_bpm_control &&
                                      parser.initial_division_control && parser.tick == 0U &&
                                      bpm == parser.chart.metadata.whole_bpm_milli &&
                                      cell.substr(close + 1U, 3U) == "{4}";
            if (bpm == parser.bpm_milli && !conventional) {
                parser.add_warning(WarningCode::RedundantBpm, location);
            }
            if (bpm < kMinBpmMilli || bpm > kMaxBpmMilli) {
                parser.add_error(SemanticErrorCode::BpmRange, location, bpm);
            }
            parser.bpm_milli = bpm;
            parser.initial_bpm_control = false;
            parser.conventional_division_pending = conventional;
        } else {
            const std::uint32_t division = parse_uint<std::uint32_t>(value, location, "division");
            if (division == 0U || kTicksPerWhole % division != 0U) {
                throw FormatError("division must be nonzero and exact on the 384-tick grid",
                                  location.line, location.column);
            }
            const bool conventional = parser.conventional_division_pending && division == 4U;
            if (division == parser.division && !conventional) {
                parser.add_warning(WarningCode::RedundantDivision, location);
            }
            parser.division = division;
            parser.initial_division_control = false;
            parser.conventional_division_pending = false;
        }
        cursor = close + 1U;
    }
}

void parse_cell(BodyParser& parser, std::size_t begin, std::size_t end, bool has_comma) {
    std::string cell;
    cell.reserve(end - begin);
    for (std::size_t index = begin; index < end; ++index) {
        cell.push_back(parser.body[index].value);
    }
    if (cell == "E") {
        if (has_comma || parser.ended) {
            const SourceLocation location = location_at(parser.body, begin);
            throw FormatError("E must be the final cell without a trailing comma", location.line,
                              location.column);
        }
        parser.chart.cells.push_back(ChartCell{"E", location_at(parser.body, begin)});
        parser.ended = true;
        return;
    }
    if (parser.ended) {
        const SourceLocation location = location_at(parser.body, begin);
        throw FormatError("body content follows E", location.line, location.column);
    }

    parser.chart.cells.push_back(ChartCell{cell, location_at(parser.body, begin)});
    std::size_t cursor = 0;
    parse_controls(parser, cell, begin, cursor);
    std::size_t note_count = 0;
    if (cursor < cell.size()) {
        std::size_t note_begin = cursor;
        while (note_begin <= cell.size()) {
            const std::size_t slash = cell.find('/', note_begin);
            const std::size_t note_end = slash == std::string_view::npos ? cell.size() : slash;
            parse_note(parser, std::string_view(cell).substr(note_begin, note_end - note_begin),
                       begin + note_begin);
            ++note_count;
            if (slash == std::string_view::npos) {
                break;
            }
            note_begin = slash + 1U;
        }
    }
    if (note_count >= kDenseEachThreshold) {
        parser.add_warning(WarningCode::DenseEach, location_at(parser.body, begin + cursor));
    }

    const std::uint64_t step = kTicksPerWhole / parser.division;
    parser.tick += step;
    if (parser.tick > kMaxTimelineTick) {
        const std::size_t position = has_comma && end < parser.body.size() ? end : begin;
        parser.add_error(SemanticErrorCode::TimelineRange, location_at(parser.body, position),
                         saturated_u32(parser.tick));
    }
    parser.chart.max_tick = std::max(parser.chart.max_tick, saturated_u32(parser.tick));
}

[[nodiscard]] bool valid_metadata_value(std::string_view value) noexcept {
    return !value.empty() && value.front() != ' ' && value.back() != ' ' &&
           std::all_of(value.begin(), value.end(), [](char character) {
               const auto byte = static_cast<unsigned char>(character);
               return byte >= 0x20U && byte <= 0x7eU;
           });
}

[[nodiscard]] std::vector<std::string_view> strict_lines(std::string_view text) {
    if (text.empty() || text.size() > kMaxChartBytes || text.back() != '\n') {
        throw FormatError("maidata must be 1..8 MiB and end with LF");
    }
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    std::size_t line_number = 1;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const unsigned char byte = static_cast<unsigned char>(text[index]);
        if (byte != '\n' && (byte < 0x20U || byte > 0x7eU)) {
            throw FormatError("maidata must use printable ASCII and LF", line_number,
                              index - start + 1U);
        }
        if (byte != '\n') {
            continue;
        }
        const std::string_view line = text.substr(start, index - start);
        const std::size_t limit = line_number <= 7U ? kMaxMetadataLineBytes
                                                    : kMaxChartLineBytes;
        if (line.size() > limit) {
            throw FormatError("maidata line exceeds its byte limit", line_number, limit + 1U);
        }
        if (line.empty()) {
            throw FormatError("maidata does not permit empty physical lines", line_number, 1);
        }
        if (line.back() == ' ') {
            throw FormatError("maidata line has trailing whitespace", line_number, line.size());
        }
        lines.push_back(line);
        start = index + 1U;
        ++line_number;
    }
    return lines;
}

[[nodiscard]] std::string_view metadata_value(std::string_view line,
                                              std::string_view prefix,
                                              std::size_t line_number) {
    if (!line.starts_with(prefix)) {
        throw FormatError("metadata field is missing or out of order", line_number, 1);
    }
    const std::string_view value = line.substr(prefix.size());
    if (!valid_metadata_value(value)) {
        throw FormatError("metadata value must be nonempty canonical printable ASCII",
                          line_number, prefix.size() + 1U);
    }
    return value;
}

}

bool valid_semantic_error_code(SemanticErrorCode code) noexcept {
    const auto value = static_cast<std::uint16_t>(code);
    return value >= 1U && value <= 6U;
}

ParsedChart parse_maidata(std::string_view text) {
    const std::vector<std::string_view> lines = strict_lines(text);
    if (lines.size() < 8U) {
        throw FormatError("maidata requires seven metadata lines and a body", lines.size() + 1U,
                          1);
    }

    ParsedChart chart;
    chart.metadata.title = std::string(metadata_value(lines[0], "&title=", 1));
    chart.metadata.artist = std::string(metadata_value(lines[1], "&artist=", 2));
    chart.metadata.description = std::string(metadata_value(lines[2], "&des=", 3));
    if (lines[3] != "&first=0") {
        throw FormatError("fourth metadata line must be exactly &first=0", 4, 1);
    }
    constexpr std::string_view whole_prefix = "&wholebpm=";
    if (!lines[4].starts_with(whole_prefix)) {
        throw FormatError("fifth metadata line must be &wholebpm=", 5, 1);
    }
    chart.metadata.whole_bpm_milli = parse_bpm_milli(
        lines[4].substr(whole_prefix.size()), SourceLocation{5, 11,
        static_cast<std::uint32_t>(text.find(whole_prefix) + whole_prefix.size())});
    if (chart.metadata.whole_bpm_milli < kMinBpmMilli ||
        chart.metadata.whole_bpm_milli > kMaxBpmMilli) {
        throw FormatError("wholebpm must be in 30..400 BPM", 5, 11);
    }
    chart.metadata.level = std::string(metadata_value(lines[5], "&lv_1=", 6));
    if (lines[6] != "&inote_1=") {
        throw FormatError("seventh metadata line must be exactly &inote_1=", 7, 1);
    }

    std::vector<PositionedChar> body;
    std::size_t absolute_offset = 0;
    for (std::size_t line = 0; line < 7U; ++line) {
        absolute_offset += lines[line].size() + 1U;
    }
    for (std::size_t line = 7U; line < lines.size(); ++line) {
        if (line + 1U < lines.size() && lines[line].back() != ',') {
            throw FormatError("continued body lines must end at a comma", line + 1U,
                              lines[line].size());
        }
        for (std::size_t column = 0; column < lines[line].size(); ++column) {
            if (lines[line][column] == ' ') {
                throw FormatError("body does not permit whitespace", line + 1U, column + 1U);
            }
            body.push_back(PositionedChar{
                lines[line][column],
                SourceLocation{static_cast<std::uint32_t>(line + 1U),
                               static_cast<std::uint32_t>(column + 1U),
                               static_cast<std::uint32_t>(absolute_offset + column)}});
        }
        absolute_offset += lines[line].size() + 1U;
    }

    BodyParser parser{chart, body};
    parser.bpm_milli = chart.metadata.whole_bpm_milli;
    std::size_t cell_begin = 0;
    for (std::size_t index = 0; index <= body.size(); ++index) {
        if (index != body.size() && body[index].value != ',') {
            continue;
        }
        parse_cell(parser, cell_begin, index, index != body.size());
        cell_begin = index + 1U;
    }
    if (!parser.ended) {
        const SourceLocation location = location_at(body, body.size() - 1U);
        throw FormatError("chart must terminate with E", location.line, location.column);
    }
    if (chart.events.empty() && chart.counts.total == 0U) {
        chart.warnings.push_back(Warning{WarningCode::EmptyChart, 1,
                                         chart.cells.front().location});
    }

    std::sort(chart.events.begin(), chart.events.end(), [](const Event& lhs, const Event& rhs) {
        return std::tuple(lhs.tick, static_cast<std::uint8_t>(lhs.type),
                          lhs.location.byte_offset) <
               std::tuple(rhs.tick, static_cast<std::uint8_t>(rhs.type),
                          rhs.location.byte_offset);
    });
    std::sort(chart.semantic_candidates.begin(), chart.semantic_candidates.end(),
              [](const SemanticError& lhs, const SemanticError& rhs) {
                  return std::tuple(lhs.location.byte_offset,
                                    static_cast<std::uint16_t>(lhs.code)) <
                         std::tuple(rhs.location.byte_offset,
                                    static_cast<std::uint16_t>(rhs.code));
              });
    chart.counts.warning = static_cast<std::uint32_t>(chart.warnings.size());
    return chart;
}

std::string write_maidata(const ParsedChart& chart) {
    if (!valid_metadata_value(chart.metadata.title) ||
        !valid_metadata_value(chart.metadata.artist) ||
        !valid_metadata_value(chart.metadata.description) ||
        !valid_metadata_value(chart.metadata.level) ||
        chart.metadata.whole_bpm_milli < kMinBpmMilli ||
        chart.metadata.whole_bpm_milli > kMaxBpmMilli || chart.cells.empty()) {
        throw FormatError("cannot write non-canonical maidata metadata or an absent AST");
    }
    if (select_semantic_error(chart) != nullptr) {
        throw FormatError("cannot write a chart with semantic errors");
    }

    std::string output;
    output += "&title=" + chart.metadata.title + '\n';
    output += "&artist=" + chart.metadata.artist + '\n';
    output += "&des=" + chart.metadata.description + '\n';
    output += "&first=0\n";
    output += "&wholebpm=" + bpm_text(chart.metadata.whole_bpm_milli) + '\n';
    output += "&lv_1=" + chart.metadata.level + '\n';
    output += "&inote_1=\n";
    std::string body_line;
    for (std::size_t index = 0; index < chart.cells.size(); ++index) {
        const std::string token = chart.cells[index].canonical +
                                  (index + 1U < chart.cells.size() ? "," : "");
        if (token.size() > kMaxChartLineBytes) {
            throw FormatError("canonical body cell exceeds the line limit");
        }
        if (!body_line.empty() && body_line.size() + token.size() > kMaxChartLineBytes) {
            output += body_line + '\n';
            body_line.clear();
        }
        body_line += token;
    }
    output += body_line + '\n';
    if (output.size() > kMaxChartBytes) {
        throw FormatError("canonical maidata exceeds 8 MiB");
    }
    const ParsedChart reparsed = parse_maidata(output);
    bool warnings_match = reparsed.warnings.size() == chart.warnings.size();
    for (std::size_t index = 0; warnings_match && index < chart.warnings.size(); ++index) {
        warnings_match = reparsed.warnings[index].code == chart.warnings[index].code &&
                         reparsed.warnings[index].severity == chart.warnings[index].severity;
    }
    bool events_match = reparsed.events.size() == chart.events.size();
    for (std::size_t index = 0; events_match && index < chart.events.size(); ++index) {
        const Event& lhs = reparsed.events[index];
        const Event& rhs = chart.events[index];
        events_match = lhs.type == rhs.type && lhs.tick == rhs.tick &&
                       lhs.end_tick == rhs.end_tick && lhs.lane == rhs.lane &&
                       lhs.target_lane == rhs.target_lane &&
                       lhs.touch_sensor == rhs.touch_sensor && lhs.strength == rhs.strength &&
                       lhs.path_id == rhs.path_id && lhs.canonical == rhs.canonical;
    }
    if (reparsed.counts != chart.counts || reparsed.max_tick != chart.max_tick ||
        !warnings_match || !events_match) {
        throw FormatError("maidata AST, events, counts, and warnings are inconsistent");
    }
    return output;
}

const SemanticError* select_semantic_error(const ParsedChart& chart) noexcept {
    if (chart.semantic_candidates.empty()) {
        return nullptr;
    }
    return &*std::min_element(
        chart.semantic_candidates.begin(), chart.semantic_candidates.end(),
        [](const SemanticError& lhs, const SemanticError& rhs) {
            return std::tuple(lhs.location.byte_offset,
                              static_cast<std::uint16_t>(lhs.code)) <
                   std::tuple(rhs.location.byte_offset,
                              static_cast<std::uint16_t>(rhs.code));
        });
}

}
