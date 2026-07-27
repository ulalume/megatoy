#include "../test_check.hpp"
#include "formats/ginpkg.hpp"
#include "formats/patch_loader.hpp"
#include "formats/patch_registry.hpp"
#include "formats/ym2612_format_adapter.hpp"
#include "ym2612/patch.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

ym2612::Patch make_sample_patch() {
  ym2612::Patch patch;
  patch.name = "roundtrip";
  patch.channel.left_speaker = true;
  patch.channel.right_speaker = true;
  patch.instrument.feedback = 3;
  patch.instrument.algorithm = 2;

  // Give every operator a distinct value in every field, so a writer that
  // mixes up operator order or drops a field cannot pass by coincidence.
  for (int op = 0; op < 4; ++op) {
    auto &settings = patch.instrument.operators[op];
    settings.attack_rate = static_cast<uint8_t>(10 + op);
    settings.decay_rate = static_cast<uint8_t>(5 + op);
    settings.sustain_rate = static_cast<uint8_t>(3 + op);
    settings.release_rate = static_cast<uint8_t>(2 + op);
    settings.sustain_level = static_cast<uint8_t>(4 + op);
    settings.total_level = static_cast<uint8_t>(20 + op * 7);
    settings.key_scale = static_cast<uint8_t>(op % 4);
    settings.multiple = static_cast<uint8_t>(1 + op);
  }
  return patch;
}

// The instrument parameters every supported format is able to represent.
// Formats differ on panning, LFO and detune, so those are checked only where
// the format claims to carry them.
void check_core_matches(const ym2612::Patch &expected,
                        const ym2612::Patch &actual, const std::string &label) {
  if (expected.instrument.algorithm != actual.instrument.algorithm ||
      expected.instrument.feedback != actual.instrument.feedback) {
    std::cerr << label << ": algorithm/feedback mismatch\n";
    CHECK(false);
  }
  for (int op = 0; op < 4; ++op) {
    const auto &a = expected.instrument.operators[op];
    const auto &b = actual.instrument.operators[op];
    const bool same =
        a.attack_rate == b.attack_rate && a.decay_rate == b.decay_rate &&
        a.sustain_rate == b.sustain_rate && a.release_rate == b.release_rate &&
        a.sustain_level == b.sustain_level && a.total_level == b.total_level &&
        a.key_scale == b.key_scale && a.multiple == b.multiple;
    if (!same) {
      std::cerr << label << ": operator " << op << " mismatch\n";
      CHECK(false);
    }
  }
}

void clean_dir(const std::filesystem::path &dir) {
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
}

// Formats that carry the full megatoy patch, so equality must hold exactly.
bool is_lossless(const std::string &extension) {
  return extension == ".gin" || extension == ".ginpkg" ||
         extension == ".dmp" || extension == ".fui" || extension == ".mml";
}

} // namespace

int main() {
  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "megatoy_format_roundtrip";
  clean_dir(tmp);

  const auto patch = make_sample_patch();
  auto &registry = formats::PatchRegistry::instance();

  // patch -> ginpkg -> load. megatoy's own versioned container.
  const auto ginpkg_path =
      formats::ginpkg::save_patch(tmp, patch, "pkg_roundtrip").value();
  const auto ginpkg_loaded = formats::load_patch_from_file(ginpkg_path);
  CHECK(ginpkg_loaded.status == formats::PatchLoadStatus::Success);
  CHECK(ginpkg_loaded.patches.size() == 1);
  CHECK(ginpkg_loaded.patches[0] == patch);

  // Every writable format the registry offers has to survive a save/load
  // cycle. Driving this from export_formats() means a format added upstream
  // is covered here the moment it appears.
  const auto formats = registry.export_formats();
  CHECK(!formats.empty());

  std::vector<std::string> covered;
  for (const auto &info : formats) {
    const auto path = tmp / ("roundtrip" + info.extension);

    const bool wrote = info.is_text
                           ? registry.write_text(info.extension, patch, path)
                           : registry.write(info.extension, patch, path);
    if (!wrote) {
      std::cerr << info.extension << ": write failed\n";
      CHECK(false);
    }

    const auto loaded = formats::load_patch_from_file(path);
    if (loaded.status == formats::PatchLoadStatus::Failure) {
      std::cerr << info.extension << ": load failed -- " << loaded.message
                << "\n";
      CHECK(false);
    }
    CHECK(!loaded.patches.empty());

    check_core_matches(patch, loaded.patches[0], info.extension);
    if (is_lossless(info.extension)) {
      if (!(loaded.patches[0] == patch)) {
        std::cerr << info.extension << ": expected an exact round trip\n";
        CHECK(false);
      }
    }

    covered.push_back(info.extension);
    std::cout << "  " << info.extension << " (" << info.label << ") ok\n";
  }

  // Guard against the registry quietly losing a format.
  for (const char *required : {".dmp", ".fui", ".mml", ".gin"}) {
    CHECK(std::find(covered.begin(), covered.end(), required) != covered.end());
  }

  // Formats gained by moving to ym2612_format must be readable.
  const auto readable = formats::adapter::readable_extensions();
  for (const char *required : {".dmf", ".fur", ".opm", ".tfi", ".rym2612"}) {
    CHECK(std::find(readable.begin(), readable.end(), required) !=
          readable.end());
  }

  std::cout << "patch_io_roundtrip_test passed\n";
  return 0;
}
