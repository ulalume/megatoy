// The rules that decide whether Save may write back to a file, and the
// writer that does it. Getting can_overwrite_in_place wrong is destructive
// in one direction (a bank overwritten by a single patch) and annoying in
// the other (a pointless Save As dialog), so the table is pinned here.

#include "patches/patch_write.hpp"
#include "formats/patch_loader.hpp"
#include "ym2612/patch.hpp"

#include "../test_check.hpp"
#include <filesystem>
#include <iostream>

int main() {
  using patches::can_overwrite_in_place;

  // Single-patch writable formats: safe to overwrite.
  CHECK(can_overwrite_in_place("song.dmp"));
  CHECK(can_overwrite_in_place("song.fui"));
  CHECK(can_overwrite_in_place("song.gin"));
  CHECK(can_overwrite_in_place("song.tfi"));
  CHECK(can_overwrite_in_place("SONG.DMP")); // case-insensitive

  // megatoy's container appends a version instead of replacing.
  CHECK(can_overwrite_in_place("song.ginpkg"));

  // Banks hold other instruments a single patch would destroy.
  CHECK(!can_overwrite_in_place("module.fur"));
  CHECK(!can_overwrite_in_place("module.dmf"));
  CHECK(!can_overwrite_in_place("bank.opm"));
  CHECK(!can_overwrite_in_place("song.mml"));

  // Read-only and unknown formats cannot be written at all.
  CHECK(!can_overwrite_in_place("preset.rym2612"));
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

  for (const char *name : {"out.dmp", "out.gin", "out.mml", "out.ginpkg"}) {
    const auto path = dir / name;
    CHECK(patches::write_patch(patch, path));
    auto loaded = formats::load_patch_from_file(
        name == std::string("out.ginpkg") ? path : path);
    CHECK(loaded.status == formats::PatchLoadStatus::Success ||
          loaded.status == formats::PatchLoadStatus::MultiInstrument);
    CHECK(!loaded.patches.empty());
    CHECK(loaded.patches[0].instrument.algorithm == 5);
  }

  std::filesystem::remove_all(dir);
  std::cout << "All patch write tests passed\n";
  return 0;
}
