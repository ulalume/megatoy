// The rules that decide whether Save may write back to a file, and the
// writer that does it. Getting can_overwrite_in_place wrong is destructive
// in one direction (a bank overwritten by a single patch) and annoying in
// the other (a pointless Save As dialog), so the table is pinned here.

#include "formats/patch_loader.hpp"
#include "patches/patch_write.hpp"
#include "ym2612/patch.hpp"

#include "../test_check.hpp"
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main() {
  using patches::can_overwrite_in_place;

  // Single-patch writable formats: safe to overwrite.
  CHECK(can_overwrite_in_place("song.dmp"));
  CHECK(can_overwrite_in_place("song.fui"));
  CHECK(can_overwrite_in_place("song.tfi"));
  CHECK(can_overwrite_in_place("song.vgi"));
  CHECK(can_overwrite_in_place("song.eif"));
  CHECK(can_overwrite_in_place("SONG.DMP")); // case-insensitive

  // megatoy's container appends a version instead of replacing.
  CHECK(can_overwrite_in_place("song.ginpkg"));

  // Read only.
  CHECK(!can_overwrite_in_place("song.gin"));

  // Banks hold other instruments a single patch would destroy.
  CHECK(!can_overwrite_in_place("module.fur"));
  CHECK(!can_overwrite_in_place("module.dmf"));
  CHECK(!can_overwrite_in_place("bank.opm"));
  CHECK(!can_overwrite_in_place("song.mml"));

  // Read-only and unknown formats cannot be written at all.
  CHECK(!can_overwrite_in_place("preset.rym2612"));
  CHECK(!can_overwrite_in_place("song.vgm"));
  CHECK(!can_overwrite_in_place("song.vgz"));
  CHECK(!can_overwrite_in_place("file.wav"));
  CHECK(!can_overwrite_in_place("noextension"));

  // write_patch round-trips through the extension-selected format.
  const auto dir = std::filesystem::temp_directory_path() / "megatoy_write";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);

  ym2612::Patch patch;
  patch.name = "write-test";
  patch.instrument.algorithm = 5;
  patch.instrument.operators[0].attack_rate = 20;

  for (const char *name : {"out.dmp", "out.mml", "out.ginpkg"}) {
    const auto path = dir / name;
    CHECK(patches::write_patch(patch, path));
    auto loaded = formats::load_patch_from_file(
        name == std::string("out.ginpkg") ? path : path);
    CHECK(loaded.status == formats::PatchLoadStatus::Success ||
          loaded.status == formats::PatchLoadStatus::MultiInstrument);
    CHECK(!loaded.patches.empty());
    CHECK(loaded.patches[0].instrument.algorithm == 5);
  }

  // The save dialog offers every format once, the patch's own first.
  const std::vector<formats::SaveFormatInfo> formats{
      {".dmp", "DefleMask Preset", false},
      {".fui", "Furnace Instrument", false},
      {".mml", "MML", true},
      {".dmp", "DefleMask Preset", true}};
  const auto filters = patches::save_dialog_filters(formats, ".fui");
  CHECK(filters.size() == 3);
  CHECK(filters[0].extensions == std::vector<std::string>{"fui"});
  CHECK(filters[1].extensions == std::vector<std::string>{"dmp"});
  CHECK(filters[2].label == formats[2].display_name());

  // The format is the one the returned name has, or else the one offered.
  CHECK(patches::with_save_extension("lead.FUI", formats, ".dmp") ==
        std::filesystem::path("lead.FUI"));
  CHECK(patches::with_save_extension("lead", formats, ".dmp") ==
        std::filesystem::path("lead.dmp"));
  CHECK(patches::with_save_extension("lead.txt", formats, ".dmp") ==
        std::filesystem::path("lead.txt.dmp"));

  std::filesystem::remove_all(dir);
  std::cout << "All patch write tests passed\n";
  return 0;
}
