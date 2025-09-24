#include "patch_io.hpp"
#include "parsers/util.hpp"
#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

namespace ym2612 {

std::filesystem::path build_patch_path(const std::filesystem::path &patches_dir,
                                       const std::string &filename) {
  std::string full_filename = filename;
  if (full_filename.find(".gin") == std::string::npos) {
    full_filename += ".gin";
  }
  return patches_dir / full_filename;
}

const std::optional<std::filesystem::path>
save_patch(const std::filesystem::path &patches_dir, const Patch &patch,
           const std::string &filename) {
  try {
    auto filepath = build_patch_path(patches_dir, filename);
    nlohmann::json j = patch;

    std::ofstream file(filepath);
    if (!file) {
      std::cerr << "Failed to open file for writing: " << filepath << std::endl;
      return std::nullopt;
    }

    file << j.dump(2);
    std::cout << "Saved patch to: " << filepath << std::endl;
    return filepath;
  } catch (const std::exception &e) {
    std::cerr << "Save error: " << e.what() << std::endl;
    return std::nullopt;
  }
}

bool load_patch(const std::filesystem::path &patches_dir, Patch &patch,
                const std::string &filename) {
  try {
    auto filepath = build_patch_path(patches_dir, filename);

    if (!std::filesystem::exists(filepath)) {
      std::cerr << "File does not exist: " << filepath << std::endl;
      return false;
    }

    std::ifstream file(filepath);
    if (!file) {
      std::cerr << "Failed to open file for reading: " << filepath << std::endl;
      return false;
    }

    nlohmann::json j;
    file >> j;
    patch = j.get<Patch>();

    std::cout << "Loaded patch from: " << filepath << std::endl;
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Load error: " << e.what() << std::endl;
    return false;
  }
}

std::vector<std::string>
list_patch_files(const std::filesystem::path &patches_dir) {
  std::vector<std::string> files;

  if (!std::filesystem::exists(patches_dir)) {
    return files;
  }

  try {
    for (const auto &entry : std::filesystem::directory_iterator(patches_dir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".gin") {
        files.push_back(entry.path().stem().string());
      }
    }
  } catch (const std::filesystem::filesystem_error &e) {
    std::cerr << "Error listing files: " << e.what() << std::endl;
  }

  return files;
}

bool export_patch_as_ctrmml(const Patch &patch,
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

    const std::string instrument_name =
        patch.name.empty() ? "Instrument" : patch.name;

    out << "@1 fm ; " << instrument_name << "\n";
    out << ";  ALG  FB\n";
    out << "  " << std::setw(2) << static_cast<int>(patch.instrument.algorithm)
        << "   " << static_cast<int>(patch.instrument.feedback) << "\n";
    out << ";  AR  DR  SR  RR  SL  TL  KS  ML  DT SSG\n";

    const std::array<std::string, 4> op_labels = {
        "S1",
        "S3",
        "S2",
        "S4",
    };

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
      out << std::setfill(' ');
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

    return true;
  } catch (const std::exception &e) {
    std::cerr << "Failed to export patch to ctrmml text: " << e.what()
              << std::endl;
    return false;
  }
}

bool export_patch_as_dmp(const Patch &patch,
                         const std::filesystem::path &target_path) {
  try {
    std::filesystem::path output_path = target_path;
    if (output_path.extension().empty()) {
      output_path.replace_extension(".dmp");
    }

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
      data.push_back(parsers::convert_detune_from_patch_to_dmp_fui(
          op.detune & 0x07)); // DT2 unsupported
      data.push_back(std::min<uint8_t>(op.sustain_rate, 31));
      uint8_t ssg =
          (op.ssg_enable ? 0x08 : 0x00) | (op.ssg_type_envelope_control & 0x07);
      data.push_back(ssg);
    }

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

} // namespace ym2612
