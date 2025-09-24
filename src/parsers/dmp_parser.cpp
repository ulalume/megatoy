#include "dmp_parser.hpp"
#include "util.hpp"
#include <fstream>
#include <iostream>

namespace parsers {

// Helper that reads a value from the binary stream
template <typename T> T read_binary_value(std::ifstream &file) {
  T value;
  file.read(reinterpret_cast<char *>(&value), sizeof(T));
  return value;
}

// Skip a fixed number of bytes
void skip_bytes(std::ifstream &file, size_t count) {
  file.seekg(count, std::ios::cur);
}

bool parse_dmp_file(const std::filesystem::path &file_path,
                    ym2612::Patch &patch) {
  std::ifstream file(file_path, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Failed to open DMP file: " << file_path << std::endl;
    return false;
  }

  try {
    // Validate file version
    uint8_t file_version = read_binary_value<uint8_t>(file);
    if (file_version != 11) {
      std::cerr << "Unsupported DMP version: " << static_cast<int>(file_version)
                << std::endl;
      return false;
    }

    // Validate the target system
    uint8_t system = read_binary_value<uint8_t>(file);
    if (system != 0x02) { // SYSTEM_GENESIS
      std::cerr << "Unsupported system: " << static_cast<int>(system)
                << std::endl;
      return false;
    }

    // Validate the instrument mode
    uint8_t instrument_mode = read_binary_value<uint8_t>(file);
    if (instrument_mode != 1) { // FM mode
      std::cerr << "Only FM instruments are supported" << std::endl;
      return false;
    }

    // Set the patch name based on the filename
    patch.name = file_path.stem().string();
    const auto parent = file_path.parent_path().filename().string();
    patch.category = parent == "patches" ? "dmp" : parent;

    // Default global settings
    patch.global.dac_enable = false;
    patch.global.lfo_enable = false;
    patch.global.lfo_frequency = 0;

    // Read FM parameters
    uint8_t lfo_fms = read_binary_value<uint8_t>(file);   // FMS on YM2612
    uint8_t feedback = read_binary_value<uint8_t>(file);  // FB
    uint8_t algorithm = read_binary_value<uint8_t>(file); // ALG
    uint8_t lfo_ams = read_binary_value<uint8_t>(file);   // AMS on YM2612

    // Configure the channel
    patch.channel.left_speaker = true;
    patch.channel.right_speaker = true;
    patch.channel.amplitude_modulation_sensitivity = lfo_ams & 0x03;
    patch.channel.frequency_modulation_sensitivity = lfo_fms & 0x07;

    // Configure the instrument
    patch.instrument.algorithm = algorithm & 0x07;
    patch.instrument.feedback = feedback & 0x07;

    // Configure the four operators
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

      // Apply operator parameters
      operator_settings.multiple = mult & 0x0F;
      operator_settings.total_level = tl & 0x7F;
      operator_settings.attack_rate = ar & 0x1F;
      operator_settings.decay_rate = dr & 0x1F;
      operator_settings.sustain_level = sl & 0x0F;
      operator_settings.release_rate = rr & 0x0F;
      operator_settings.amplitude_modulation_enable = (am != 0);
      operator_settings.key_scale = rs & 0x03;
      operator_settings.detune = parsers::convert_detune_from_dmp_to_patch(dt);
      operator_settings.sustain_rate = d2r & 0x1F;

      // Handle SSGEG bits
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

    return true;

  } catch (const std::exception &e) {
    std::cerr << "Error parsing DMP file " << file_path << ": " << e.what()
              << std::endl;
    return false;
  }
}

std::string get_dmp_patch_name(const std::filesystem::path &file_path) {
  // DMP files do not store an internal name,
  // so the filename is used as-is
  return file_path.stem().string();
}

} // namespace parsers
