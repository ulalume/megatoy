#include "dmp.hpp"

#include "common.hpp"
#include "ym2612/types.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

// Helper that reads a value from the binary stream
template <typename T> T read_binary_value(std::ifstream &file) {
  T value;
  file.read(reinterpret_cast<char *>(&value), sizeof(T));
  return value;
}

} // namespace

namespace formats::dmp {

std::vector<ym2612::Patch> read_file(const std::filesystem::path &file_path) {
  std::ifstream file(file_path, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Failed to open DMP file: " << file_path << std::endl;
    return {};
  }

  try {
    uint8_t file_version = read_binary_value<uint8_t>(file);
    if (file_version != 11) {
      std::cerr << "Unsupported DMP version: " << static_cast<int>(file_version)
                << std::endl;
      return {};
    }

    uint8_t system = read_binary_value<uint8_t>(file);
    if (system != 0x02) { // SYSTEM_GENESIS
      std::cerr << "Unsupported system: " << static_cast<int>(system)
                << std::endl;
      return {};
    }

    uint8_t instrument_mode = read_binary_value<uint8_t>(file);
    if (instrument_mode != 1) { // FM mode
      std::cerr << "Only FM instruments are supported" << std::endl;
      return {};
    }
    ym2612::Patch patch;

    patch.name = file_path.stem().string();

    patch.global.dac_enable = false;
    patch.global.lfo_enable = false;
    patch.global.lfo_frequency = 0;

    uint8_t lfo_fms = read_binary_value<uint8_t>(file);   // FMS on YM2612
    uint8_t feedback = read_binary_value<uint8_t>(file);  // FB
    uint8_t algorithm = read_binary_value<uint8_t>(file); // ALG
    uint8_t lfo_ams = read_binary_value<uint8_t>(file);   // AMS on YM2612

    patch.channel.left_speaker = true;
    patch.channel.right_speaker = true;
    patch.channel.amplitude_modulation_sensitivity = lfo_ams & 0x03;
    patch.channel.frequency_modulation_sensitivity = lfo_fms & 0x07;

    patch.instrument.algorithm = algorithm & 0x07;
    patch.instrument.feedback = feedback & 0x07;

    for (int op = 0; op < 4; ++op) {
      auto &operator_settings = patch.instrument.operators[op];

      uint8_t mult = read_binary_value<uint8_t>(file);  // MULT
      uint8_t tl = read_binary_value<uint8_t>(file);    // TL
      uint8_t ar = read_binary_value<uint8_t>(file);    // AR
      uint8_t dr = read_binary_value<uint8_t>(file);    // DR (D1R)
      uint8_t sl = read_binary_value<uint8_t>(file);    // SL (D2L)
      uint8_t rr = read_binary_value<uint8_t>(file);    // RR
      uint8_t am = read_binary_value<uint8_t>(file);    // AM
      uint8_t rs = read_binary_value<uint8_t>(file);    // RS (Key Scale)
      uint8_t dt = read_binary_value<uint8_t>(file);    // DT
      uint8_t d2r = read_binary_value<uint8_t>(file);   // D2R
      uint8_t ssgeg = read_binary_value<uint8_t>(file); // SSGEG

      operator_settings.multiple = mult & 0x0F;
      operator_settings.total_level = tl & 0x7F;
      operator_settings.attack_rate = ar & 0x1F;
      operator_settings.decay_rate = dr & 0x1F;
      operator_settings.sustain_level = sl & 0x0F;
      operator_settings.release_rate = rr & 0x0F;
      operator_settings.amplitude_modulation_enable = (am != 0);
      operator_settings.key_scale = rs & 0x03;
      operator_settings.detune = formats::detune_from_dmp_to_patch(dt);
      operator_settings.sustain_rate = d2r & 0x1F;

      bool ssgeg_enabled = (ssgeg & 0x08) != 0;
      uint8_t ssgeg_mode = ssgeg & 0x07;

      operator_settings.ssg_enable = ssgeg_enabled;
      operator_settings.ssg_type_envelope_control = ssgeg_mode;

      std::cout << "DMP OP" << (op + 1) << ": MULT=" << static_cast<int>(mult)
                << " TL=" << static_cast<int>(tl)
                << " AR=" << static_cast<int>(ar)
                << " DR=" << static_cast<int>(dr)
                << " SL=" << static_cast<int>(sl)
                << " RR=" << static_cast<int>(rr)
                << " AM=" << static_cast<int>(am)
                << " RS=" << static_cast<int>(rs)
                << " DT=" << static_cast<int>(dt)
                << " D2R=" << static_cast<int>(d2r)
                << " SSGEG=" << static_cast<int>(ssgeg) << std::endl;
    }

    std::cout << "Successfully parsed DMP file: " << file_path.filename()
              << std::endl;
    std::cout << "Algorithm=" << static_cast<int>(algorithm)
              << " Feedback=" << static_cast<int>(feedback)
              << " FMS=" << static_cast<int>(lfo_fms)
              << " AMS=" << static_cast<int>(lfo_ams) << std::endl;

    return {patch};

  } catch (const std::exception &e) {
    std::cerr << "Error parsing DMP file " << file_path << ": " << e.what()
              << std::endl;
    return {};
  }
}

std::vector<uint8_t> serialize_patch(const ym2612::Patch &patch) {
  std::vector<uint8_t> data;
  data.reserve(3 + 4 * 11);
  data.push_back(0x0B); // version
  data.push_back(0x02); // system: Genesis
  data.push_back(0x01); // instrument mode FM
  data.push_back(patch.channel.frequency_modulation_sensitivity & 0x07);
  data.push_back(patch.instrument.feedback & 0x07);
  data.push_back(patch.instrument.algorithm & 0x07);
  data.push_back(patch.channel.amplitude_modulation_sensitivity & 0x03);

  for (size_t op_idx = 0; op_idx < ym2612::all_operator_indices.size();
       ++op_idx) {
    const auto &op = patch.instrument.operators[op_idx];

    data.push_back(op.multiple & 0x0F);
    data.push_back(std::min<uint8_t>(op.total_level, 127));
    data.push_back(std::min<uint8_t>(op.attack_rate, 31));
    data.push_back(std::min<uint8_t>(op.decay_rate, 31));
    data.push_back(std::min<uint8_t>(op.sustain_level, 15));
    data.push_back(std::min<uint8_t>(op.release_rate, 15));
    data.push_back(op.amplitude_modulation_enable ? 1 : 0);
    data.push_back(std::min<uint8_t>(op.key_scale, 3));
    data.push_back(formats::detune_from_patch_to_dmp(op.detune & 0x07));
    data.push_back(std::min<uint8_t>(op.sustain_rate, 31));
    uint8_t ssg =
        (op.ssg_enable ? 0x08 : 0x00) | (op.ssg_type_envelope_control & 0x07);
    data.push_back(ssg);
  }
  return data;
}

bool write_patch(const ym2612::Patch &patch,
                 const std::filesystem::path &target_path) {
  try {
    std::filesystem::path output_path = target_path;
    if (output_path.extension().empty()) {
      output_path.replace_extension(".dmp");
    }

    auto data = serialize_patch(patch);
    std::ofstream out(output_path, std::ios::binary);
    if (!out) {
      std::cerr << "Failed to open file for writing: " << output_path
                << std::endl;
      return false;
    }

    out.write(reinterpret_cast<const char *>(data.data()), data.size());
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Failed to export patch to DMP: " << e.what() << std::endl;
    return false;
  }
}

std::string get_patch_name(const std::filesystem::path &file_path) {
  return file_path.stem().string();
}

} // namespace formats::dmp
