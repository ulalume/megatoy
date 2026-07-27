#include "formats/gin.hpp"
#include "formats/ginpkg.hpp"
#include "formats/patch_loader.hpp"
#include "formats/patch_registry.hpp"
#include "ym2612/patch.hpp"
#include "../test_check.hpp"
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

  // patch -> ginpkg -> load
  auto ginpkg_path =
      formats::ginpkg::save_patch(tmp, patch, "pkg_roundtrip").value();
  auto ginpkg_loaded = formats::load_patch_from_file(ginpkg_path);
  CHECK(ginpkg_loaded.status == formats::PatchLoadStatus::Success);
  CHECK(ginpkg_loaded.patches.size() == 1);
  CHECK(ginpkg_loaded.patches[0] == patch);

  // patch -> dmp -> load
  std::filesystem::path dmp_path = tmp / "roundtrip.dmp";
  bool wrote_dmp =
      formats::PatchRegistry::instance().write(".dmp", patch, dmp_path);
  CHECK(wrote_dmp);
  auto dmp_loaded = formats::load_patch_from_file(dmp_path);
  CHECK(dmp_loaded.status == formats::PatchLoadStatus::Success);
  CHECK(dmp_loaded.patches.size() == 1);
  CHECK(dmp_loaded.patches[0] == patch);

  // patch -> mml -> load
  std::filesystem::path mml_path = tmp / "roundtrip.mml";
  bool wrote_mml =
      formats::PatchRegistry::instance().write_text(".mml", patch, mml_path);
  CHECK(wrote_mml);
  auto mml_loaded = formats::load_patch_from_file(mml_path);
  CHECK(mml_loaded.status == formats::PatchLoadStatus::Success);
  CHECK(mml_loaded.patches.size() >= 1);
  CHECK(mml_loaded.patches[0] == patch);

  // patch -> fui -> load
  std::filesystem::path fui_path = tmp / "roundtrip.fui";
  bool wrote_fui =
      formats::PatchRegistry::instance().write(".fui", patch, fui_path);
  CHECK(wrote_fui);
  auto fui_loaded = formats::load_patch_from_file(fui_path);
  CHECK(fui_loaded.status == formats::PatchLoadStatus::Success);
  CHECK(fui_loaded.patches.size() == 1);
  CHECK(fui_loaded.patches[0] == patch);

  std::cout << "patch_io_roundtrip_test passed\n";
  return 0;
}
