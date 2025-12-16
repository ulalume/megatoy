#include "formats/patch_loader.hpp"
#include "formats/patch_registry.hpp"
#include "formats/gin.hpp"
#include "formats/ginpkg.hpp"
#include "ym2612/patch.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>

namespace {

ym2612::Patch make_sample_patch() {
  ym2612::Patch patch;
  patch.name = "roundtrip";
  patch.channel.left_speaker = true;
  patch.channel.right_speaker = true;
  patch.instrument.feedback = 3;
  patch.instrument.algorithm = 2;
  patch.instrument.operators[0].attack_rate = 10;
  patch.instrument.operators[1].attack_rate = 11;
  patch.instrument.operators[2].attack_rate = 12;
  patch.instrument.operators[3].attack_rate = 13;
  return patch;
}

void clean_dir(const std::filesystem::path &dir) {
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
}

} // namespace

int main() {
  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "megatoy_format_roundtrip";
  clean_dir(tmp);

  auto patch = make_sample_patch();

  // gin -> ginpkg -> load
  auto ginpkg_path =
      formats::ginpkg::save_patch(tmp, patch, "pkg_roundtrip").value();
  auto ginpkg_loaded = formats::load_patch_from_file(ginpkg_path);
  assert(ginpkg_loaded.status == formats::PatchLoadStatus::Success);
  assert(ginpkg_loaded.patches.size() == 1);
  assert(ginpkg_loaded.patches[0] == patch);

  // gin -> dmp -> gin
  std::filesystem::path dmp_path = tmp / "roundtrip.dmp";
  bool wrote_dmp = formats::PatchRegistry::instance().write(
      ".dmp", patch, dmp_path);
  assert(wrote_dmp);
  auto dmp_loaded = formats::load_patch_from_file(dmp_path);
  assert(dmp_loaded.status == formats::PatchLoadStatus::Success);
  assert(dmp_loaded.patches.size() == 1);
  assert(dmp_loaded.patches[0] == patch);

  // gin -> mml -> gin
  std::filesystem::path mml_path = tmp / "roundtrip.mml";
  bool wrote_mml = formats::PatchRegistry::instance().write_text(
      ".mml", patch, mml_path);
  assert(wrote_mml);
  auto mml_loaded = formats::load_patch_from_file(mml_path);
  assert(mml_loaded.status == formats::PatchLoadStatus::Success);
  assert(mml_loaded.patches.size() >= 1);
  assert(mml_loaded.patches[0] == patch);

  std::cout << "patch_io_roundtrip_test passed\n";
  return 0;
}
