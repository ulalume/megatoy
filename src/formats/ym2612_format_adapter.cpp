#include "formats/ym2612_format_adapter.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

namespace formats::adapter {

namespace {

std::vector<uint8_t> read_bytes(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),
                              std::istreambuf_iterator<char>());
}

bool write_bytes(const std::filesystem::path &path,
                 const std::vector<uint8_t> &bytes) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  file.write(reinterpret_cast<const char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(file);
}

std::string to_lower(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

// Detune registers 0 and 4 both mean "no detune". Formats that store a linear
// -3..+3 scale cannot tell them apart and the library decodes the zero point
// as 4, which would make every round trip of a detune-0 patch look like an
// edit. Settling on 0 keeps saving and reloading a no-op.
uint8_t canonical_detune(uint8_t detune) { return detune == 4 ? 0 : detune; }

} // namespace

ym2612_format::Patch to_library(const ym2612::Patch &patch) {
  ym2612_format::Patch out;
  out.name = patch.name;

  out.dac_enable = patch.global.dac_enable;
  out.lfo_enable = patch.global.lfo_enable;
  out.lfo_frequency = patch.global.lfo_frequency;

  out.left = patch.channel.left_speaker;
  out.right = patch.channel.right_speaker;
  out.ams = patch.channel.amplitude_modulation_sensitivity;
  out.fms = patch.channel.frequency_modulation_sensitivity;

  out.algorithm = patch.instrument.algorithm;
  out.feedback = patch.instrument.feedback;

  for (std::size_t i = 0; i < 4; ++i) {
    const auto &source = patch.instrument.operators[i];
    auto &target = out.operators[i];
    target.ar = source.attack_rate;
    target.dr = source.decay_rate;
    target.sr = source.sustain_rate;
    target.rr = source.release_rate;
    target.sl = source.sustain_level;
    target.tl = source.total_level;
    target.ks = source.key_scale;
    target.ml = source.multiple;
    target.dt = source.detune;
    target.ssg = source.ssg_type_envelope_control;
    target.ssg_enable = source.ssg_enable;
    target.am = source.amplitude_modulation_enable;
    target.enable = source.enable;
  }

  return out;
}

ym2612::Patch from_library(const ym2612_format::Patch &patch) {
  ym2612::Patch out;
  out.name = patch.name;

  out.global.dac_enable = patch.dac_enable;
  out.global.lfo_enable = patch.lfo_enable;
  out.global.lfo_frequency = patch.lfo_frequency;

  out.channel.left_speaker = patch.left;
  out.channel.right_speaker = patch.right;
  out.channel.amplitude_modulation_sensitivity = patch.ams;
  out.channel.frequency_modulation_sensitivity = patch.fms;

  out.instrument.algorithm = patch.algorithm;
  out.instrument.feedback = patch.feedback;

  for (std::size_t i = 0; i < 4; ++i) {
    const auto &source = patch.operators[i];
    auto &target = out.instrument.operators[i];
    target.attack_rate = source.ar;
    target.decay_rate = source.dr;
    target.sustain_rate = source.sr;
    target.release_rate = source.rr;
    target.sustain_level = source.sl;
    target.total_level = source.tl;
    target.key_scale = source.ks;
    target.multiple = source.ml;
    target.detune = canonical_detune(source.dt);
    target.ssg_type_envelope_control = source.ssg;
    target.ssg_enable = source.ssg_enable;
    target.amplitude_modulation_enable = source.am;
    target.enable = source.enable;
  }

  // patch.macros / patch.operator_macros are intentionally dropped: megatoy
  // edits a single static patch and has nowhere to put a macro sequence.

  return out;
}

std::string extension_for(ym2612_format::Format format) {
  return std::string(".") + ym2612_format::format_to_extension(format);
}

std::optional<ym2612_format::Format>
format_for_extension(const std::string &extension) {
  std::string normalized = to_lower(extension);
  if (!normalized.empty() && normalized.front() == '.') {
    normalized.erase(normalized.begin());
  }
  return ym2612_format::format_from_string(normalized);
}

const std::vector<ym2612_format::FormatInfo> &known_formats() {
  static const std::vector<ym2612_format::FormatInfo> formats =
      ym2612_format::all_formats();
  return formats;
}

const std::vector<std::string> &readable_extensions() {
  static const std::vector<std::string> extensions = [] {
    std::vector<std::string> result;
    for (const auto &info : known_formats()) {
      if (info.can_read) {
        result.push_back("." + info.extension);
      }
    }
    std::sort(result.begin(), result.end());
    return result;
  }();
  return extensions;
}

bool is_multi_patch(ym2612_format::Format format) {
  switch (format) {
  case ym2612_format::Format::Mml:    // one instrument per @ definition
  case ym2612_format::Format::Dmf:    // module instrument list
  case ym2612_format::Format::Fur:    // module instrument list
  case ym2612_format::Format::Opm:    // VOPM bank
  case ym2612_format::Format::Ginpkg: // current patch plus its history
    return true;
  default:
    return false;
  }
}

uint8_t detune_to_linear(uint8_t register_detune) {
  return ym2612_format::detune_to_linear(register_detune);
}

uint8_t detune_from_linear(int linear) {
  return canonical_detune(ym2612_format::detune_from_linear(linear));
}

std::vector<ym2612::Patch> read_file(ym2612_format::Format format,
                                     const std::filesystem::path &path) {
  const auto bytes = read_bytes(path);
  if (bytes.empty()) {
    return {};
  }

  auto result = ym2612_format::parse_as(format, bytes.data(), bytes.size(),
                                        path.stem().string());
  if (!ym2612_format::is_ok(result)) {
    return {};
  }

  const auto &ok = ym2612_format::get_ok(result);
  std::vector<ym2612::Patch> patches;
  patches.reserve(ok.patches.size());
  for (const auto &patch : ok.patches) {
    patches.push_back(from_library(patch));
  }
  return patches;
}

std::optional<std::vector<uint8_t>> serialize(ym2612_format::Format format,
                                              const ym2612::Patch &patch) {
  auto result = ym2612_format::serialize(format, to_library(patch));
  if (!ym2612_format::is_ok(result)) {
    return std::nullopt;
  }
  return ym2612_format::get_ok(result);
}

std::optional<std::string> serialize_text(ym2612_format::Format format,
                                          const ym2612::Patch &patch) {
  auto result = ym2612_format::serialize_text(format, to_library(patch));
  if (!ym2612_format::is_ok(result)) {
    return std::nullopt;
  }
  return ym2612_format::get_ok(result);
}

bool write_file(ym2612_format::Format format, const ym2612::Patch &patch,
                const std::filesystem::path &path) {
  auto bytes = serialize(format, patch);
  if (!bytes) {
    return false;
  }
  return write_bytes(path, *bytes);
}

} // namespace formats::adapter
