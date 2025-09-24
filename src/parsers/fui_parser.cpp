#include "fui_parser.hpp"

#include "../ym2612/types.hpp"
#include "util.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr std::array<char, 4> kFinsMagic{'F', 'I', 'N', 'S'};
constexpr std::array<char, 16> kOldMagic{'-', 'F', 'u', 'r', 'n', 'a',
                                         'c', 'e', ' ', 'i', 'n', 's',
                                         't', 'r', '.', '-'};

bool parse_fm_feature(const std::vector<uint8_t> &data, ym2612::Patch &result) {
  if (data.size() < 4) {
    std::cerr << "FUI FM block too small\n";
    return false;
  }

  const uint8_t flags = data[0];
  const uint8_t op_count_raw = flags & 0x0F;
  const uint8_t op_count =
      std::min<uint8_t>(op_count_raw ? op_count_raw : 4, 4);

  const uint8_t alg_fb = data[1];
  result.instrument.algorithm = (alg_fb >> 4) & 0x07;
  result.instrument.feedback = alg_fb & 0x07;

  const uint8_t fms_ams = data[2];
  result.channel.frequency_modulation_sensitivity = fms_ams & 0x07;
  result.channel.amplitude_modulation_sensitivity = (fms_ams >> 3) & 0x03;

  // Remaining byte currently unused (LLPatch / advanced features).

  size_t offset = 4;
  const size_t stride = 8;

  for (uint8_t i = 0; i < op_count; ++i, offset += stride) {
    if (offset + stride > data.size()) {
      std::cerr << "FUI FM block truncated for operator " << static_cast<int>(i)
                << "\n";
      return false;
    }

    auto &op = result.instrument.operators[i];

    const uint8_t reg_30 = data[offset + 0];
    const uint8_t reg_40 = data[offset + 1];
    const uint8_t reg_50 = data[offset + 2];
    const uint8_t reg_60 = data[offset + 3];
    const uint8_t reg_70 = data[offset + 4];
    const uint8_t reg_80 = data[offset + 5];
    const uint8_t reg_90 = data[offset + 6];
    const uint8_t reg_94 = data[offset + 7];

    op.detune = parsers::convert_detune_from_dmp_to_patch(reg_30 >> 4);
    op.multiple = reg_30 & 0x0F;

    op.total_level = reg_40 & 0x7F;

    op.attack_rate = reg_50 & 0x1F;
    op.decay_rate = reg_60 & 0x1F;   // D1R
    op.sustain_rate = reg_70 & 0x1F; // D2R
    op.release_rate = reg_80 & 0x0F;
    op.sustain_level = (reg_80 >> 4) & 0x0F;

    op.key_scale = (reg_60 >> 5) & 0x03; // KSL

    op.amplitude_modulation_enable = (reg_60 >> 7) & 0x01;
    const uint8_t ssg = reg_90 & 0x0F;
    op.ssg_enable = (ssg & 0x08) != 0;
    op.ssg_type_envelope_control = ssg & 0x07;
  }

  return true;
}

std::vector<uint8_t> read_file_bytes(const std::filesystem::path &file_path) {
  std::ifstream file(file_path, std::ios::binary);
  if (!file) {
    return {};
  }
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
}

template <typename T>
T read_le(const std::vector<uint8_t> &buffer, size_t offset) {
  if (offset + sizeof(T) > buffer.size()) {
    return T{};
  }
  T value{};
  for (size_t i = 0; i < sizeof(T); ++i) {
    value |= static_cast<T>(buffer[offset + i]) << (8 * i);
  }
  return value;
}

bool parse_old_fui(const std::vector<uint8_t> &bytes,
                   const std::filesystem::path &file_path,
                   ym2612::Patch &patch) {
  if (bytes.size() < kOldMagic.size() + 8) {
    std::cout << "Invalid file size: " << file_path << "\n";
    return false;
  }

  if (!std::equal(kOldMagic.begin(), kOldMagic.end(), bytes.begin())) {
    std::cout << "Legacy FUI header is invalid: " << file_path << "\n";
    return false;
  }

  const uint32_t instrument_ptr = read_le<uint32_t>(bytes, 16 + 4);
  if (instrument_ptr >= bytes.size()) {
    std::cerr << "Legacy FUI header index is invalid: " << file_path << "\n";
    return false;
  }

  size_t pos = instrument_ptr;
  if (pos + 8 > bytes.size() || bytes[pos] != 'I' || bytes[pos + 1] != 'N' ||
      bytes[pos + 2] != 'S' || bytes[pos + 3] != 'T') {
    std::cerr << "Legacy FUI is missing an INST block: " << file_path << "\n";
    return false;
  }

  pos += 4;
  const uint32_t block_size = read_le<uint32_t>(bytes, pos);
  pos += 4;
  const size_t block_end =
      (block_size != 0 && instrument_ptr + 8u + block_size <= bytes.size())
          ? instrument_ptr + 8u + block_size
          : bytes.size();

  if (pos + 2 > block_end) {
    std::cerr << "Legacy FUI instrument block is invalid: " << file_path
              << "\n";
    return false;
  }

  const uint16_t data_version = read_le<uint16_t>(bytes, pos);
  pos += 2;

  if (pos >= block_end) {
    std::cerr << "Legacy FUI instrument block is invalid (secondary check): "
              << file_path << "\n";
    return false;
  }

  const uint8_t instrument_type = bytes[pos++];
  pos += 1; // reserved

  if (instrument_type != 1) {
    std::cerr << "Legacy FUI instrument was not FM (OPN): " << instrument_type
              << "\n";
    return false;
  }

  size_t name_end = pos;
  while (name_end < block_end && bytes[name_end] != 0) {
    ++name_end;
  }

  std::string name(bytes.begin() + pos, bytes.begin() + name_end);
  pos = (name_end < block_end) ? name_end + 1 : block_end;

  ym2612::Patch result{};
  result.name = name.empty() ? file_path.stem().string() : name;
  result.global = {
      .dac_enable = false, .lfo_enable = false, .lfo_frequency = 0};
  result.channel.left_speaker = true;
  result.channel.right_speaker = true;
  result.channel.amplitude_modulation_sensitivity = 0;
  result.channel.frequency_modulation_sensitivity = 0;
  for (auto &op : result.instrument.operators) {
    op = {};
  }

  if (pos + 8 > block_end) {
    std::cerr << "Legacy FUI FM block lacks required data: " << file_path
              << "\n";
    return false;
  }

  result.instrument.algorithm = bytes[pos++] & 0x07;
  result.instrument.feedback = bytes[pos++] & 0x07;
  result.channel.frequency_modulation_sensitivity = bytes[pos++] & 0x07;
  result.channel.amplitude_modulation_sensitivity = bytes[pos++] & 0x03;

  const uint8_t operator_count = bytes[pos++];
  (void)operator_count;

  if (data_version >= 60) {
    pos += 1; // OPLL preset (unused for YM2612)
  } else {
    pos += 1;
  }

  pos += 2; // reserved

  for (int i = 0; i < 4; ++i) {
    if (pos + 20 > block_end) {
      break;
    }

    const uint8_t am = bytes[pos++];
    const uint8_t attack_rate = bytes[pos++];
    const uint8_t decay_rate = bytes[pos++];
    const uint8_t multiple = bytes[pos++];
    const uint8_t release_rate = bytes[pos++];
    const uint8_t sustain_level = bytes[pos++];
    const uint8_t total_level = bytes[pos++];
    const uint8_t dt2 = bytes[pos++];
    (void)dt2;
    const uint8_t rs = bytes[pos++];
    (void)rs;
    const uint8_t detune = bytes[pos++];
    const uint8_t sustain_rate = bytes[pos++];
    const uint8_t ssg_env = bytes[pos++];
    const uint8_t dam = bytes[pos++];
    (void)dam;
    const uint8_t dvb = bytes[pos++];
    (void)dvb;
    const uint8_t egt = bytes[pos++];
    (void)egt;
    const uint8_t key_scale = bytes[pos++];
    const uint8_t sus = bytes[pos++];
    (void)sus;
    const uint8_t vib = bytes[pos++];
    (void)vib;
    const uint8_t ws = bytes[pos++];
    (void)ws;
    const uint8_t ksr = bytes[pos++];
    (void)ksr;

    if (data_version >= 114) {
      pos += 1; // enable flag (unused)
    } else {
      pos += 1;
    }

    if (data_version >= 115) {
      pos += 1; // kvs (unused)
    } else {
      pos += 1;
    }

    const size_t reserved_bytes = 10;
    if (pos + reserved_bytes > block_end) {
      break;
    }
    pos += reserved_bytes;

    auto &op = result.instrument.operators[i];
    op.amplitude_modulation_enable = (am != 0);
    op.attack_rate = std::min<uint8_t>(attack_rate, 31);
    op.decay_rate = std::min<uint8_t>(decay_rate, 31);
    op.multiple = std::min<uint8_t>(multiple, 15);
    op.release_rate = std::min<uint8_t>(release_rate, 15);
    op.sustain_level = std::min<uint8_t>(sustain_level, 15);
    op.total_level = std::min<uint8_t>(total_level, 127);
    op.detune = parsers::convert_detune_from_dmp_to_patch(detune);
    op.sustain_rate = std::min<uint8_t>(sustain_rate, 31);
    const bool ssg_enable = (ssg_env & 0x10) != 0;
    op.ssg_enable = ssg_enable;
    op.ssg_type_envelope_control = ssg_enable ? (ssg_env & 0x07) : 0;
    op.key_scale = key_scale & 0x03;
  }

  if (result.category.empty()) {
    result.category = file_path.parent_path().filename().string();
  }

  patch = std::move(result);
  return true;
}

bool parse_new_fui(const std::vector<uint8_t> &bytes,
                   const std::filesystem::path &file_path,
                   ym2612::Patch &patch) {
  if (bytes.size() < 8 ||
      !std::equal(kFinsMagic.begin(), kFinsMagic.end(), bytes.begin())) {
    return false;
  }

  const uint16_t instrument_type =
      static_cast<uint16_t>(bytes[6] | (bytes[7] << 8));
  if (instrument_type != 1) {
    std::cerr << "FUI instrument is not FM (OPN): " << file_path << "\n";
    return false;
  }

  ym2612::Patch result{};
  result.name.clear();
  result.category.clear();
  result.global = {
      .dac_enable = false, .lfo_enable = false, .lfo_frequency = 0};
  result.channel.left_speaker = true;
  result.channel.right_speaker = true;
  result.channel.amplitude_modulation_sensitivity = 0;
  result.channel.frequency_modulation_sensitivity = 0;
  for (auto &op : result.instrument.operators) {
    op = {};
  }

  size_t pos = 8;
  bool fm_loaded = false;
  bool name_loaded = false;

  while (pos + 4 <= bytes.size()) {
    const uint16_t feature_code =
        static_cast<uint16_t>(bytes[pos] | (bytes[pos + 1] << 8));
    const uint16_t feature_length =
        static_cast<uint16_t>(bytes[pos + 2] | (bytes[pos + 3] << 8));
    pos += 4;

    if (pos + feature_length > bytes.size()) {
      std::cerr << "FUI feature overruns file: " << file_path << "\n";
      return false;
    }

    const char code_chars[3] = {static_cast<char>(feature_code & 0xFF),
                                static_cast<char>((feature_code >> 8) & 0xFF),
                                '\0'};
    const std::string feature_name(code_chars);

    const std::vector<uint8_t> payload(bytes.begin() + pos,
                                       bytes.begin() + pos + feature_length);
    pos += feature_length;

    if (feature_name == "NA") {
      const auto zero_pos =
          std::find(payload.begin(), payload.end(), static_cast<uint8_t>(0));
      result.name.assign(payload.begin(), zero_pos);
      name_loaded = true;
    } else if (feature_name == "FM") {
      fm_loaded = parse_fm_feature(payload, result);
    } else if (feature_name == "EN") {
      break;
    }
  }

  if (!name_loaded) {
    result.name = file_path.stem().string();
  }

  if (result.category.empty()) {
    const auto parent = file_path.parent_path().filename().string();
    result.category = parent;
  }

  if (fm_loaded) {
    patch = std::move(result);
  }

  return fm_loaded;
}

} // namespace

namespace parsers {

bool parse_fui_file(const std::filesystem::path &file_path,
                    ym2612::Patch &patch) {
  const auto bytes = read_file_bytes(file_path);
  if (bytes.empty()) {
    std::cerr << "FUI file could not be read: " << file_path << "\n";
    return false;
  }

  if (parse_new_fui(bytes, file_path, patch)) {
    return true;
  }

  if (parse_old_fui(bytes, file_path, patch)) {
    return true;
  }

  std::cerr << "Unsupported FUI format: " << file_path << "\n";
  return false;
}

} // namespace parsers
