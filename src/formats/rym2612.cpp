#include "rym2612.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace formats::rym2612 {

// Convert rym2612 detune into the app format
static uint8_t convert_detune(int rym_dt) {
  switch (rym_dt) {
  case -3:
    return 7;
  case -2:
    return 6;
  case -1:
    return 5;
  case 0:
    return 4; // rym2612 zero maps to two values; default to 4
  case 1:
    return 1;
  case 2:
    return 2;
  case 3:
    return 3;
  default:
    return 4; // Default fallback
  }
}

uint8_t convert_multiple(int rym_mul) {
  // std::cout << "rym_mul: " << rym_mul << std::endl;
  float mul = static_cast<float>(rym_mul);
  float normalized = round(mul / 1000.0f);
  if (normalized < 0) {
    normalized = 0;
  } else if (normalized > 15) {
    normalized = 15;
  }
  return static_cast<uint8_t>(normalized);
}

// Convert SSGEG value
void convert_ssgeg(int rym_ssgeg, bool &ssg_enable, uint8_t &ssg_type) {
  if (rym_ssgeg == 0) {
    ssg_enable = false;
    ssg_type = 0;
  } else if (rym_ssgeg >= 1 && rym_ssgeg <= 8) {
    ssg_enable = true;
    ssg_type = static_cast<uint8_t>(rym_ssgeg - 1);
  } else {
    ssg_enable = false;
    ssg_type = 0;
  }
}

// Lightweight XML extractor
std::string extract_xml_value(const std::string &xml_content,
                              const std::string &tag) {
  // rym2612 format: <PARAM id="tag" value="..."/>
  std::string param_pattern = "<PARAM id=\"" + tag + "\" value=\"";

  size_t start = xml_content.find(param_pattern);
  if (start == std::string::npos)
    return "";

  start += param_pattern.length();
  size_t end = xml_content.find("\"", start);
  if (end == std::string::npos)
    return "";

  return xml_content.substr(start, end - start);
}

// Convert a string to a number with error handling
template <typename T>
T safe_convert(const std::string &str, T default_value = T{}) {
  try {
    if constexpr (std::is_same_v<T, int>) {
      return std::stoi(str);
    } else if constexpr (std::is_same_v<T, float>) {
      return std::stof(str);
    } else if constexpr (std::is_same_v<T, uint8_t>) {
      int val = std::stoi(str);
      return static_cast<uint8_t>(std::clamp(val, 0, 255));
    }
  } catch (const std::exception &) {
    return default_value;
  }
  return default_value;
}

std::vector<ym2612::Patch> read_file(const std::filesystem::path &file_path) {
  std::ifstream file(file_path);
  if (!file.is_open()) {
    std::cerr << "Failed to open rym2612 file: " << file_path << std::endl;
    return {};
  }

  // Read the entire file
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string xml_content = buffer.str();

  std::cout << "Parsing rym2612 file: " << file_path << std::endl;

  try {
    ym2612::Patch patch;
    auto extract_attribute = [&](const std::string &attribute) {
      const std::string pattern = attribute + "=\"";
      size_t start = xml_content.find(pattern);
      if (start == std::string::npos) {
        return std::string{};
      }
      start += pattern.length();
      size_t end = xml_content.find("\"", start);
      if (end == std::string::npos) {
        return std::string{};
      }
      return xml_content.substr(start, end - start);
    };

    // Get the patch name from the attribute
    std::string name = extract_attribute("patchName");

    if (name.empty()) {
      patch.name = file_path.stem().string();
    } else {
      patch.name = name;
    }

    // Global settings
    patch.global.dac_enable = false; // rym2612 does not expose DAC settings
    patch.global.lfo_enable =
        safe_convert<int>(extract_xml_value(xml_content, "LFO_Enable")) != 0;
    patch.global.lfo_frequency =
        safe_convert<uint8_t>(extract_xml_value(xml_content, "LFO_Speed"), 0);

    // Channel settings
    patch.channel.left_speaker = true;  // Default
    patch.channel.right_speaker = true; // Default
    patch.channel.amplitude_modulation_sensitivity =
        safe_convert<uint8_t>(extract_xml_value(xml_content, "AMS"), 0);
    patch.channel.frequency_modulation_sensitivity =
        safe_convert<uint8_t>(extract_xml_value(xml_content, "FMS"), 0);

    // Instrument settings
    patch.instrument.algorithm =
        safe_convert<uint8_t>(extract_xml_value(xml_content, "Algorithm"), 1) -
        1;
    patch.instrument.feedback =
        safe_convert<uint8_t>(extract_xml_value(xml_content, "Feedback"), 0);

    // Operator settings
    for (int i = 1; i <= 4; ++i) {
      auto &operator_settings = patch.instrument.operators[static_cast<uint8_t>(
          ym2612::all_operator_indices[i - 1])];

      // Core parameters
      std::string ar_val =
          extract_xml_value(xml_content, "OP" + std::to_string(i) + "AR");
      std::string d1r_val =
          extract_xml_value(xml_content, "OP" + std::to_string(i) + "D1R");
      std::string d2r_val =
          extract_xml_value(xml_content, "OP" + std::to_string(i) + "D2R");
      std::string rr_val =
          extract_xml_value(xml_content, "OP" + std::to_string(i) + "RR");
      std::string d2l_val =
          extract_xml_value(xml_content, "OP" + std::to_string(i) + "D2L");

      std::cout << "OP" << i << " AR=" << ar_val << " D1R=" << d1r_val
                << " D2R=" << d2r_val << " RR=" << rr_val << " D2L=" << d2l_val
                << std::endl;

      operator_settings.attack_rate = safe_convert<uint8_t>(ar_val, 0);
      operator_settings.decay_rate = safe_convert<uint8_t>(d1r_val, 0);
      operator_settings.sustain_rate = safe_convert<uint8_t>(d2r_val, 0);
      operator_settings.release_rate = safe_convert<uint8_t>(rr_val, 0);
      operator_settings.sustain_level = 15 - safe_convert<uint8_t>(d2l_val, 0);

      // Total level (non-linear conversion)
      std::string tl_val =
          extract_xml_value(xml_content, "OP" + std::to_string(i) + "TL");
      std::string vel_val =
          extract_xml_value(xml_content, "OP" + std::to_string(i) + "Vel");

      std::cout << "OP" << i << " TL=" << tl_val << " Vel=" << vel_val
                << std::endl;

      float base_tl = safe_convert<float>(tl_val, 0);
      float velocity = safe_convert<float>(vel_val, 0);

      // Apply velocity
      base_tl += velocity;

      // Apply volume
      int final_tl = static_cast<int>(std::round(base_tl));

      // Invert TL (app.tl = 127 - TL)
      operator_settings.total_level =
          static_cast<uint8_t>(127 - std::clamp(final_tl, 0, 127));

      std::string ks_val = extract_xml_value(
          xml_content, "OP" + std::to_string(i) + "RS"); // Note: RS in rym2612
      operator_settings.key_scale = safe_convert<uint8_t>(ks_val, 0);

      // Multiple (may be scaled by 1000)
      std::string mul_val =
          extract_xml_value(xml_content, "OP" + std::to_string(i) + "MUL");
      int raw_mul = safe_convert<int>(mul_val, 0);
      operator_settings.multiple = convert_multiple(raw_mul);

      // Detune (non-linear mapping)
      std::string dt_val =
          extract_xml_value(xml_content, "OP" + std::to_string(i) + "DT");
      int raw_dt = safe_convert<int>(dt_val, 0);
      operator_settings.detune = convert_detune(raw_dt);

      // SSGEG
      std::string ssgeg_val =
          extract_xml_value(xml_content, "OP" + std::to_string(i) + "SSGEG");
      int raw_ssgeg = safe_convert<int>(ssgeg_val, 0);
      convert_ssgeg(raw_ssgeg, operator_settings.ssg_enable,
                    operator_settings.ssg_type_envelope_control);

      // AM
      std::string am_val =
          extract_xml_value(xml_content, "OP" + std::to_string(i) + "AM");
      operator_settings.amplitude_modulation_enable =
          safe_convert<int>(am_val) != 0;

      std::cout << "OP" << i << " RS=" << ks_val << " MUL=" << mul_val
                << " DT=" << dt_val << " SSGEG=" << ssgeg_val
                << " AM=" << am_val << std::endl;
    }

    return {patch};
  } catch (const std::exception &e) {
    std::cerr << "Error parsing rym2612 file " << file_path << ": " << e.what()
              << std::endl;
    return {};
  }
}

std::string get_patch_name(const std::filesystem::path &file_path) {
  std::ifstream file(file_path);
  if (!file.is_open()) {
    return file_path.stem().string();
  }

  // Read only the first few KB to locate the name
  std::string content;
  content.resize(2048);
  file.read(&content[0], content.size());
  content.resize(file.gcount());

  // Extract the name from the patchName attribute
  std::string name;
  size_t name_start = content.find("patchName=\"");
  if (name_start != std::string::npos) {
    name_start += 11; // Length of "patchName=\""
    size_t name_end = content.find("\"", name_start);
    if (name_end != std::string::npos) {
      name = content.substr(name_start, name_end - name_start);
    }
  }

  if (name.empty()) {
    return file_path.stem().string();
  }

  return name;
}

} // namespace formats::rym2612
