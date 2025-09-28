#include "ctrmml.hpp"

#include "../ym2612/types.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

std::string trim(const std::string &s) {
  auto first = std::find_if_not(s.begin(), s.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  });
  auto last = std::find_if_not(s.rbegin(), s.rend(), [](unsigned char ch) {
                return std::isspace(ch) != 0;
              }).base();
  if (first >= last) {
    return {};
  }
  return std::string(first, last);
}

std::vector<int> parse_numbers(const std::string &text) {
  std::string cleaned;
  cleaned.reserve(text.size());
  for (char ch : text) {
    if (ch == ',') {
      cleaned.push_back(' ');
    } else {
      cleaned.push_back(ch);
    }
  }

  std::vector<int> result;
  std::istringstream iss(cleaned);
  int value;
  while (iss >> value) {
    result.push_back(value);
  }
  return result;
}

uint8_t clamp_uint8(int value, int min, int max) {
  return static_cast<uint8_t>(std::clamp(value, min, max));
}

bool starts_with_at(const std::string &line) {
  auto first = std::find_if_not(line.begin(), line.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  });
  return first != line.end() && *first == '@';
}

} // namespace

namespace ym2612::formats::ctrmml {

bool read_file(const std::filesystem::path &file_path,
               std::vector<Instrument> &out_instruments) {
  out_instruments.clear();

  std::ifstream file(file_path);
  if (!file.is_open()) {
    std::cerr << "Failed to open ctrmml file: " << file_path << "\n";
    return false;
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }

  auto file_stem = file_path.stem().string();

  for (size_t line_index = 0; line_index < lines.size(); ++line_index) {
    const std::string &raw_line = lines[line_index];
    std::string trimmed = trim(raw_line);
    if (trimmed.empty() || trimmed.front() != '@') {
      continue;
    }

    std::string comment;
    auto comment_pos = raw_line.find(';');
    if (comment_pos != std::string::npos) {
      comment = trim(raw_line.substr(comment_pos + 1));
    }

    std::string before_comment = (comment_pos == std::string::npos)
                                     ? raw_line
                                     : raw_line.substr(0, comment_pos);

    std::istringstream header_stream(before_comment);
    char at_sign;
    int instrument_number;
    std::string fm_token;
    header_stream >> at_sign >> instrument_number >> fm_token;
    if (at_sign != '@') {
      continue;
    }

    std::string fm_token_lower = fm_token;
    std::transform(
        fm_token_lower.begin(), fm_token_lower.end(), fm_token_lower.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (fm_token_lower != "fm") {
      continue;
    }

    std::vector<int> inline_numbers;
    int value;
    while (header_stream >> value) {
      inline_numbers.push_back(value);
    }

    int algorithm = -1;
    int feedback = -1;
    if (!inline_numbers.empty()) {
      algorithm = inline_numbers[0];
      if (inline_numbers.size() > 1) {
        feedback = inline_numbers[1];
      }
    }

    std::vector<std::array<int, 10>> operator_rows;
    size_t consumed_index = line_index;

    for (size_t next = line_index + 1; next < lines.size(); ++next) {
      std::string next_raw = lines[next];
      std::string trimmed_next = trim(next_raw);

      if (trimmed_next.empty()) {
        consumed_index = next;
        continue;
      }

      if (starts_with_at(trimmed_next)) {
        consumed_index = next - 1;
        break;
      }

      auto next_comment_pos = trimmed_next.find(';');
      std::string data_part =
          next_comment_pos == std::string::npos
              ? trimmed_next
              : trim(trimmed_next.substr(0, next_comment_pos));

      if (data_part.empty()) {
        consumed_index = next;
        continue;
      }

      auto numbers = parse_numbers(data_part);
      if (numbers.empty()) {
        consumed_index = next;
        continue;
      }

      if (algorithm < 0 && numbers.size() >= 2) {
        algorithm = numbers[0];
        feedback = numbers[1];
        consumed_index = next;
        continue;
      }

      if (numbers.size() >= 10) {
        std::array<int, 10> row{};
        for (size_t idx = 0; idx < 10; ++idx) {
          row[idx] = numbers[idx];
        }
        operator_rows.push_back(row);
        consumed_index = next;
        if (operator_rows.size() == 4) {
          ++next;
          while (next < lines.size()) {
            std::string peek_trimmed = trim(lines[next]);
            if (peek_trimmed.empty() || peek_trimmed.front() == ';') {
              consumed_index = next;
              ++next;
              continue;
            }
            break;
          }
          break;
        }
        continue;
      }

      if (numbers.size() == 1) {
        consumed_index = next;
        continue;
      }

      consumed_index = next;
    }

    line_index = consumed_index;

    if (algorithm < 0) {
      algorithm = 0;
    }
    if (feedback < 0) {
      feedback = 0;
    }

    if (operator_rows.size() != 4) {
      std::cerr << "ctrmml instrument @" << instrument_number << " in file "
                << file_path
                << " does not contain 4 operator rows. Skipping.\n";
      continue;
    }

    ym2612::Patch patch{};
    patch.global = {
        .dac_enable = false, .lfo_enable = false, .lfo_frequency = 0};
    patch.channel.left_speaker = true;
    patch.channel.right_speaker = true;
    patch.channel.amplitude_modulation_sensitivity = 0;
    patch.channel.frequency_modulation_sensitivity = 0;
    patch.instrument.algorithm = clamp_uint8(algorithm, 0, 7);
    patch.instrument.feedback = clamp_uint8(feedback, 0, 7);

    for (size_t op_idx = 0; op_idx < 4; ++op_idx) {
      const auto &row = operator_rows[op_idx];
      auto &op = patch.instrument.operators[static_cast<uint8_t>(op_idx)];

      op.attack_rate = clamp_uint8(row[0], 0, 31);
      op.decay_rate = clamp_uint8(row[1], 0, 31);
      op.sustain_rate = clamp_uint8(row[2], 0, 31);
      op.release_rate = clamp_uint8(row[3], 0, 15);
      op.sustain_level = clamp_uint8(row[4], 0, 15);
      op.total_level = clamp_uint8(row[5], 0, 127);
      op.key_scale = clamp_uint8(row[6], 0, 3);
      op.multiple = clamp_uint8(row[7], 0, 15);
      op.detune = clamp_uint8(row[8], 0, 7);

      int ssg_value = row[9];
      std::cout << "SSG Value: " << ssg_value << std::endl;
      op.amplitude_modulation_enable = ssg_value >= 100;
      op.ssg_enable = (ssg_value & 0b1000) != 0;
      op.ssg_type_envelope_control = ssg_value & 0b0111;
    }

    patch.category = "ctrmml";

    std::string instrument_name = comment;
    if (instrument_name.empty()) {
      instrument_name = file_stem + "_" + std::to_string(instrument_number);
    }

    patch.name = instrument_name;

    Instrument instrument;
    instrument.instrument_number = instrument_number;
    instrument.name = instrument_name;
    instrument.patch = patch;

    out_instruments.push_back(std::move(instrument));
  }

  return !out_instruments.empty();
}

std::string patch_to_string(const ym2612::Patch &patch) {
  std::ostringstream out;

  const std::string instrument_name =
      patch.name.empty() ? "Instrument" : patch.name;

  out << "@1 fm ; " << instrument_name << "\n";
  out << ";  ALG  FB\n";
  out << "  " << std::setw(2) << static_cast<int>(patch.instrument.algorithm)
      << "   " << static_cast<int>(patch.instrument.feedback) << "\n";
  out << ";  AR  DR  SR  RR  SL  TL  KS  ML  DT SSG\n";

  const std::array<std::string, 4> op_labels = {"S1", "S3", "S2", "S4"};
  out << std::setfill(' ');

  for (size_t op_idx = 0; op_idx < ym2612::all_operator_indices.size();
       ++op_idx) {
    const auto &op = patch.instrument.operators[op_idx];

    int ssg_value = op.ssg_type_envelope_control & 0x07;
    if (op.ssg_enable) {
      ssg_value += 8;
    }
    if (op.amplitude_modulation_enable) {
      ssg_value += 100;
    }

    out << "   " << std::setw(2) << static_cast<int>(op.attack_rate) << " "
        << std::setw(3) << static_cast<int>(op.decay_rate) << " "
        << std::setw(3) << static_cast<int>(op.sustain_rate) << " "
        << std::setw(3) << static_cast<int>(op.release_rate) << " "
        << std::setw(3) << static_cast<int>(op.sustain_level) << " "
        << std::setw(3) << static_cast<int>(op.total_level) << " "
        << std::setw(3) << static_cast<int>(op.key_scale) << " "
        << std::setw(3) << static_cast<int>(op.multiple) << " "
        << std::setw(3) << static_cast<int>(op.detune) << " " << std::setw(3)
        << ssg_value << " ; " << op_labels[op_idx] << "\n";
  }

  return out.str();
}

bool write_patch(const ym2612::Patch &patch,
                 const std::filesystem::path &target_path) {
  try {
    std::filesystem::path output_path = target_path;
    if (output_path.extension().empty()) {
      output_path.replace_extension(".mml");
    }

    std::ofstream out(output_path);
    if (!out) {
      std::cerr << "Failed to open file for writing: " << output_path
                << std::endl;
      return false;
    }

    out << patch_to_string(patch);

    return true;
  } catch (const std::exception &e) {
    std::cerr << "Failed to export patch to ctrmml text: " << e.what()
              << std::endl;
    return false;
  }
}

} // namespace ym2612::formats::ctrmml
