#include "../test_check.hpp"
#include "gui/save_as_dialog.hpp"
#include "patches/filename_utils.hpp"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace {

std::vector<formats::SaveFormatInfo> formats() {
  return {
      {".gin", "megatoy"}, {".dmp", "DefleMask"}, {".vgi", "VGM Music Maker"}};
}

void test_a_writable_extension_is_kept() {
  CHECK(ui::default_save_as_extension(".gin", formats()) == ".gin");
  CHECK(ui::default_save_as_extension(".vgi", formats()) == ".vgi");
}

void test_anything_else_falls_back() {
  // Read-only formats and a patch with no file of its own alike.
  CHECK(ui::default_save_as_extension(".opm", formats()) == ".dmp");
  CHECK(ui::default_save_as_extension("", formats()) == ".dmp");
  CHECK(ui::default_save_as_extension(".gin", {}) == ".dmp");
}

void test_the_patch_folder_wins_when_it_is_writable() {
  CHECK(ui::default_save_as_folder(fs::path("/patches/mine"),
                                   fs::path("/patches/default")) ==
        fs::path("/patches/mine"));
}

void test_a_read_only_source_falls_back_to_the_default_folder() {
  CHECK(
      ui::default_save_as_folder(std::nullopt, fs::path("/patches/default")) ==
      fs::path("/patches/default"));
}

void test_the_target_is_the_folder_the_name_and_the_format() {
  CHECK(ui::save_as_target_path(fs::path("/patches/mine"), "Lead", ".dmp") ==
        fs::path("/patches/mine/Lead.dmp"));
  CHECK(ui::save_as_target_path(fs::path("/patches/mine"), "Bass", ".gin") ==
        fs::path("/patches/mine/Bass.gin"));
}

void test_a_name_the_storage_would_change_is_changed_here_too() {
  CHECK(ui::save_as_target_path(fs::path("/patches/mine"), "", ".gin") ==
        fs::path("/patches/mine/patch.gin"));
  CHECK(ui::save_as_target_path(fs::path("/patches/mine"), "a/b", ".gin") ==
        fs::path("/patches/mine") /
            (patches::sanitize_filename("a/b") + ".gin"));
}

void test_the_folder_row_appears_only_when_there_is_a_choice() {
  const std::vector<megatoy::workspace::Folder> one{
      {fs::path("/patches/only"), "only"}};
  const std::vector<megatoy::workspace::Folder> two{
      {fs::path("/patches/first"), "first"},
      {fs::path("/patches/last"), "last"}};
  CHECK(!ui::save_as_shows_folder_choice({}));
  CHECK(!ui::save_as_shows_folder_choice(one));
  CHECK(ui::save_as_shows_folder_choice(two));
}

fs::path make_root() {
  auto root = fs::temp_directory_path() / "megatoy_save_as_dialog_test";
  fs::remove_all(root);
  fs::create_directories(root / "first");
  fs::create_directories(root / "locked");
  fs::create_directories(root / "last");
  return fs::weakly_canonical(root);
}

void test_only_writable_folders_are_offered_in_order(const fs::path &root) {
  fs::permissions(root / "locked", fs::perms::owner_write,
                  fs::perm_options::remove);

  megatoy::workspace::Workspace workspace;
  workspace.set_paths({root / "first", root / "locked", root / "last"});
  CHECK(workspace.folders().size() == 3);
  CHECK(!workspace.folders()[1].writable);

  const auto choices = ui::save_as_folder_choices(workspace);
  CHECK(choices.size() == 2);
  CHECK(choices[0].path == root / "first");
  CHECK(choices[1].path == root / "last");

  fs::permissions(root / "locked", fs::perms::owner_write,
                  fs::perm_options::add);
}

} // namespace

int main() {
  const auto root = make_root();

  test_a_writable_extension_is_kept();
  test_anything_else_falls_back();
  test_the_patch_folder_wins_when_it_is_writable();
  test_a_read_only_source_falls_back_to_the_default_folder();
  test_the_target_is_the_folder_the_name_and_the_format();
  test_a_name_the_storage_would_change_is_changed_here_too();
  test_the_folder_row_appears_only_when_there_is_a_choice();
  test_only_writable_folders_are_offered_in_order(root);

  fs::remove_all(root);
  std::cout << "All Save As dialog tests passed\n";
  return 0;
}
