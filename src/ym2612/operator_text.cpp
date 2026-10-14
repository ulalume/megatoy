#include "ym2612/operator_text.hpp"
#include "ym2612/operator_edit.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace ym2612 {
namespace {

constexpr std::size_t kColumns = 10;

uint8_t clamped(long value, long min, long max) {
  return static_cast<uint8_t>(std::clamp(value, min, max));
}

bool is_separator(char c) {
  return c == ' ' || c == '\t' || c == ',' || c == '\r';
}

/// The ten values of `line`, or nothing when it is not an operator row.
std::optional<std::vector<long>> parse_row(std::string_view line) {
  line = line.substr(0, line.find(';'));

  std::vector<long> values;
  std::size_t pos = 0;
  while (pos < line.size()) {
    if (is_separator(line[pos])) {
      ++pos;
      continue;
    }
    std::size_t end = pos;
    while (end < line.size() && !is_separator(line[end])) {
      ++end;
    }
    const std::string token(line.substr(pos, end - pos));
    char *parsed_end = nullptr;
    errno = 0;
    const long value = std::strtol(token.c_str(), &parsed_end, 10);
    if (parsed_end == token.c_str() || *parsed_end != '\0' || errno != 0) {
      return std::nullopt;
    }
    values.push_back(value);
    if (values.size() > kColumns) {
      return std::nullopt;
    }
    pos = end;
  }

  if (values.size() != kColumns) {
    return std::nullopt;
  }
  return values;
}

} // namespace

std::string format_operator_line(const OperatorSettings &op, int display_slot) {
  int ssg = op.ssg_type_envelope_control & 0x07;
  if (op.ssg_enable) {
    ssg += 8;
  }
  if (op.amplitude_modulation_enable) {
    ssg += 100;
  }

  char buf[128];
  std::snprintf(buf, sizeof(buf),
                "   %2d %3d %3d %3d %3d %3d %3d %3d %3d %3d ; OP%d\n",
                static_cast<int>(op.attack_rate),
                static_cast<int>(op.decay_rate),
                static_cast<int>(op.sustain_rate),
                static_cast<int>(op.release_rate),
                static_cast<int>(op.sustain_level),
                static_cast<int>(op.total_level),
                static_cast<int>(op.key_scale), static_cast<int>(op.multiple),
                static_cast<int>(op.detune), ssg, display_slot + 1);
  return buf;
}

std::string format_operator_lines(const ChannelInstrument &instrument,
                                  const std::vector<int> &display_slots) {
  std::string out;
  for (int slot : display_slots) {
    out += format_operator_line(operator_at(instrument, slot), slot);
  }
  return out;
}

std::vector<OperatorSettings> parse_operator_lines(std::string_view text) {
  std::vector<OperatorSettings> operators;
  std::size_t pos = 0;
  while (pos <= text.size()) {
    std::size_t end = text.find('\n', pos);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    if (const auto row = parse_row(text.substr(pos, end - pos))) {
      const auto &v = *row;
      OperatorSettings op;
      op.attack_rate = clamped(v[0], 0, 31);
      op.decay_rate = clamped(v[1], 0, 31);
      op.sustain_rate = clamped(v[2], 0, 31);
      op.release_rate = clamped(v[3], 0, 15);
      op.sustain_level = clamped(v[4], 0, 15);
      op.total_level = clamped(v[5], 0, 127);
      op.key_scale = clamped(v[6], 0, 3);
      op.multiple = clamped(v[7], 0, 15);
      op.detune = clamped(v[8], 0, 7);

      long ssg = v[9];
      op.amplitude_modulation_enable = ssg >= 100;
      if (op.amplitude_modulation_enable) {
        ssg -= 100;
      }
      op.ssg_enable = (ssg & 0b1000) != 0;
      op.ssg_type_envelope_control = static_cast<uint8_t>(ssg & 0b0111);
      operators.push_back(op);
    }
    pos = end + 1;
  }
  return operators;
}

std::vector<OperatorPasteTarget>
operator_paste_targets(std::size_t row_count,
                       const std::vector<int> &selected_slots) {
  std::vector<OperatorPasteTarget> targets;
  if (row_count == 0) {
    return targets;
  }
  if (row_count == 1) {
    for (int slot : selected_slots) {
      targets.push_back({slot, 0});
    }
    return targets;
  }
  if (row_count == 4 && selected_slots.size() <= 1) {
    for (int slot = 0; slot < 4; ++slot) {
      targets.push_back({slot, static_cast<std::size_t>(slot)});
    }
    return targets;
  }
  const std::size_t count = std::min(row_count, selected_slots.size());
  for (std::size_t i = 0; i < count; ++i) {
    targets.push_back({selected_slots[i], i});
  }
  return targets;
}

} // namespace ym2612
