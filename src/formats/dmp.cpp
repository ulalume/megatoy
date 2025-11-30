#include "dmp.hpp"

#include "common.hpp"
#include "ym2612/types.hpp"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <iostream>
#include <vector>

namespace {

// Safe accessor that returns 0 when reading past the end of the buffer.
uint8_t read_byte(const std::vector<uint8_t> &bytes, size_t index) {
  if (index < bytes.size()) {
    return bytes[index];
  }
  return 0;
}

} // namespace

namespace formats::dmp {

std::vector<ym2612::Patch> read_file(const std::filesystem::path &file_path) {
  try {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
      std::cerr << "Failed to open DMP file: " << file_path << std::endl;
      return {};
    }

    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file),
                               std::istreambuf_iterator<char>()};
    constexpr size_t header_size = 7;
    constexpr size_t operator_bytes = 11;
    constexpr size_t operator_count = 4;
    constexpr size_t expected_size = header_size + operator_count * operator_bytes;

    if (bytes.size() == expected_size - 2 && bytes.size() >= 3 &&
        bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0) {
      // DefleMask sometimes writes Genesis FM presets; in this file the
      // version/system bytes were dropped. Reconstruct them so the data aligns.
      std::vector<uint8_t> repaired;
      repaired.reserve(expected_size);
      repaired.push_back(0x0B); // version
      repaired.push_back(0x02); // system: Genesis
      repaired.insert(repaired.end(), bytes.begin(), bytes.end());
      if (repaired.size() < expected_size) {
        repaired.resize(expected_size, 0);
      }
      if (repaired.size() >= 3 && repaired[2] == 0) {
        repaired[2] = 0x01; // FM instrument mode
      }
      std::cerr << "Heuristically repaired missing DMP header (version/system): "
                << file_path << std::endl;
      bytes.swap(repaired);
    }

    if (bytes.size() < 7) {
      std::cerr << "DMP file too small to contain a header: " << file_path
                << std::endl;
      return {};
    }

    struct HeaderLayout {
      size_t instrument_mode_idx;
      size_t system_idx;
      size_t fms_idx;
      size_t feedback_idx;
      size_t algorithm_idx;
      size_t ams_idx;
    };

    constexpr HeaderLayout modern_layout{2, 1, 3, 4, 5, 6};
    constexpr HeaderLayout legacy_v9_layout{1, 2, 3, 4, 5, 6};

    uint8_t file_version = read_byte(bytes, 0);
    const HeaderLayout *layout = &modern_layout;
    if (file_version == 0x09) {
      layout = &legacy_v9_layout; // pre-1.0 DefleMask exports swap system/mode
    } else if (file_version != 0x0B) {
      std::cerr << "Unrecognized DMP version " << static_cast<int>(file_version)
                << ", attempting best-effort parse" << std::endl;
    }

    uint8_t instrument_mode = read_byte(bytes, layout->instrument_mode_idx);
    uint8_t system = read_byte(bytes, layout->system_idx);
    uint8_t lfo_fms = read_byte(bytes, layout->fms_idx);       // FMS on YM2612
    uint8_t feedback = read_byte(bytes, layout->feedback_idx); // FB
    uint8_t algorithm = read_byte(bytes, layout->algorithm_idx); // ALG
    uint8_t lfo_ams = read_byte(bytes, layout->ams_idx);         // AMS on YM2612

    if (bytes.size() < expected_size) {
      std::cerr << "DMP file shorter than expected (" << bytes.size() << " < "
                << expected_size
                << "), padding missing operator bytes with zeros" << std::endl;
    }

    // Older exports used 0 or 1 for Genesis; keep loading but warn.
    if (!(system == 0x02 || (file_version <= 0x09 && (system == 0x00 || system == 0x01)))) {
      std::cerr << "Unsupported or unknown DMP system code: "
                << static_cast<int>(system) << std::endl;
    }

    if (instrument_mode != 1) {
      std::cerr << "Instrument mode is not FM (" << static_cast<int>(instrument_mode)
                << "), parsing as FM because file matches FM size" << std::endl;
    }

    ym2612::Patch patch;
    patch.name = file_path.stem().string();

    patch.global.dac_enable = false;
    patch.global.lfo_enable = false;
    patch.global.lfo_frequency = 0;

    patch.channel.left_speaker = true;
    patch.channel.right_speaker = true;
    patch.channel.amplitude_modulation_sensitivity = lfo_ams & 0x03;
    patch.channel.frequency_modulation_sensitivity = lfo_fms & 0x07;

    patch.instrument.algorithm = algorithm & 0x07;
    patch.instrument.feedback = feedback & 0x07;

    for (int op = 0; op < 4; ++op) {
      auto &operator_settings = patch.instrument.operators[op];
      const size_t base = header_size + op * operator_bytes;

      uint8_t mult = read_byte(bytes, base + 0);  // MULT
      uint8_t tl = read_byte(bytes, base + 1);    // TL
      uint8_t ar = read_byte(bytes, base + 2);    // AR
      uint8_t dr = read_byte(bytes, base + 3);    // DR (D1R)
      uint8_t sl = read_byte(bytes, base + 4);    // SL (D2L)
      uint8_t rr = read_byte(bytes, base + 5);    // RR
      uint8_t am = read_byte(bytes, base + 6);    // AM
      uint8_t rs = read_byte(bytes, base + 7);    // RS (Key Scale)
      uint8_t dt = read_byte(bytes, base + 8);    // DT
      uint8_t d2r = read_byte(bytes, base + 9);   // D2R
      uint8_t ssgeg = read_byte(bytes, base + 10); // SSGEG

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
