#include "formats/patch_registry.hpp"
#include "patches/folder_metadata.hpp"
#include "session_test_environment.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace {

using session_tests::LaunchedEnvironment;
using session_tests::find_patch_entry;

void test_workspace_folder_is_visible() {
  LaunchedEnvironment env("patch_save_test");
  const auto &folders = env.preferences.workspace().folders();
  CHECK(folders.size() == 1);
  CHECK(folders[0].available);
  CHECK(folders[0].writable);
  CHECK(env.preferences.workspace().default_save_folder().has_value());

  // The folder must show up as a root in the browser even while it is empty.
  const auto &tree = env.session.repository().tree();
  CHECK(!tree.empty());
  const bool found =
      std::any_of(tree.begin(), tree.end(), [&](const patches::PatchEntry &e) {
        return e.is_directory && e.full_path == env.folder;
      });
  CHECK(found);
}

// Saving must land in the workspace folder, and its metadata must end up in
// the folder's own sidecar rather than anywhere global.
void test_save_and_metadata_roundtrip() {
  LaunchedEnvironment env("patch_save_test");
  env.session.current_patch().name = "workspace test";
  env.session.current_patch().instrument.algorithm = 5;

  auto &repository = env.session.repository();
  const auto saved =
      repository.save_patch(env.session.current_patch(), "workspace test",
                            /*overwrite=*/true, ".ginpkg");
  CHECK(saved.status == patches::SavePatchResult::Status::Success);
  CHECK(saved.path.parent_path() == env.folder);
  CHECK(std::filesystem::exists(saved.path));

  repository.refresh();
  const auto *saved_entry = repository.find_entry(
      repository.to_relative_path(saved.path).generic_string());
  CHECK(saved_entry != nullptr);
  const auto relative = saved_entry->relative_path;

  patches::PatchMetadata metadata;
  metadata.star_rating = 4;
  metadata.category = "bass";
  CHECK(repository.update_patch_metadata(relative, metadata));

  // The sidecar lives inside the folder, not in the user's config directory.
  const auto sidecar = env.folder / ".megatoy" / "patches.json";
  CHECK(std::filesystem::exists(sidecar));

  repository.refresh();
  const auto *entry = repository.find_entry(relative);
  CHECK(entry != nullptr);
  const auto &stored = entry->metadata;
  CHECK(stored.has_value());
  CHECK(stored->star_rating == 4);
  CHECK(stored->category == "bass");

  // A fresh repository over the same folder must see the ratings again --
  // that is the point of storing them next to the patches.
  megatoy::workspace::Workspace reopened;
  CHECK(reopened.add(env.folder));
  patches::PatchRepository second(env.file_system, reopened);
  const auto *reopened_entry = second.find_entry(relative);
  CHECK(reopened_entry != nullptr);
  const auto &reloaded = reopened_entry->metadata;
  CHECK(reloaded.has_value());
  CHECK(reloaded->star_rating == 4);
}

void test_default_save_format_is_dmp() {
  LaunchedEnvironment env("patch_save_test");
  const auto saved = env.session.repository().save_patch(
      env.session.current_patch(), "default format", /*overwrite=*/true, {});
  CHECK(saved.status == patches::SavePatchResult::Status::Success);
  CHECK(saved.path.extension() == ".dmp");
}

void test_ginpkg_versions_appear_as_container_items() {
  LaunchedEnvironment env("patch_save_test");
  auto patch = env.session.current_patch();
  patch.name = "package patch";
  const auto package_path = env.folder / "versions.ginpkg";
  CHECK(patches::write_patch(patch, package_path));
  patch.instrument.algorithm = 6;
  CHECK(patches::write_patch(patch, package_path));

  env.session.repository().refresh();
  const patches::PatchEntry *container = nullptr;
  for (const auto &root : env.session.repository().tree()) {
    auto found = std::find_if(root.children.begin(), root.children.end(),
                              [&](const patches::PatchEntry &entry) {
                                return entry.full_path == package_path;
                              });
    if (found != root.children.end()) {
      container = &*found;
      break;
    }
  }
  CHECK(container != nullptr);
  CHECK(container->is_directory);
  CHECK(container->format == "ginpkg");
  CHECK(container->children.size() == 2);
  CHECK(container->children.front().container_item_id == "__current__");
  CHECK(container->children.front().source_relative_path ==
        container->relative_path);

  ym2612::Patch latest;
  ym2612::Patch previous;
  CHECK(env.session.repository().load_patch(container->children[0], latest));
  CHECK(env.session.repository().load_patch(container->children[1], previous));
  CHECK(latest.instrument.algorithm == 6);
  CHECK(previous.instrument.algorithm != latest.instrument.algorithm);

  // A package version is selected by its unique tree entry, while Save still
  // targets the shared parent package. Undo/redo must preserve both identities.
  const auto &version = container->children[1];
  env.session.set_current_patch_path(version.source_relative_path);
  CHECK(env.session.current_patch_selection_path() ==
        version.source_relative_path + "/latest");
  env.session.set_current_patch_selection_path(version.relative_path);
  CHECK(env.session.current_patch_path() == version.source_relative_path);
  CHECK(env.session.current_patch_selection_path() == version.relative_path);
  const auto snapshot = env.session.capture_snapshot();
  env.session.set_current_patch_path({});
  env.session.restore_snapshot(snapshot);
  CHECK(env.session.current_patch_path() == version.source_relative_path);
  CHECK(env.session.current_patch_selection_path() == version.relative_path);
}

// Save must not overwrite a file it cannot safely rewrite.
void test_save_in_place_rules() {
  LaunchedEnvironment env("patch_save_test");
  auto &repository = env.session.repository();
  const auto saved = repository.save_patch(
      env.session.current_patch(), "in place", /*overwrite=*/true, ".dmp");
  CHECK(saved.status == patches::SavePatchResult::Status::Success);
  repository.refresh();

  // A .dmp in a writable workspace folder is a single patch we can rewrite.
  env.session.set_current_patch_path(saved.path);
  CHECK(env.session.can_save_in_place());

  // A patch with no file behind it has to go through Save As.
  env.session.set_current_patch_path({});
  CHECK(!env.session.can_save_in_place());

  // So does one instrument taken out of a bank: rewriting the file would
  // discard every other instrument in it.
  const auto bank = env.folder / "bank.mml";
  CHECK(formats::PatchRegistry::instance().write_text(
      ".mml", env.session.current_patch(), bank));
  repository.refresh();
  env.session.set_current_patch_path(bank);
  CHECK(!env.session.can_save_in_place());
}

void test_create_patch_in_folder() {
  LaunchedEnvironment env("patch_save_test");
  auto &session = env.session;
  const auto defaults = session.default_patch();

  CHECK(session.can_create_patch_in(env.folder));
  // Only a folder the workspace owns and may be written to.
  CHECK(!session.can_create_patch_in(env.root));
  CHECK(session.create_patch_in(env.root, "nowhere", ".dmp").is_error());

  const auto created = session.create_patch_in(env.folder, "brand new", ".dmp");
  CHECK(created.is_success());
  CHECK(created.path == env.folder / "brand new.dmp");
  CHECK(std::filesystem::exists(created.path));

  // What was written is the patch a fresh launch opens, named for its file.
  CHECK(session.current_patch().name == "brand new");
  CHECK(session.current_patch().global == defaults.global);
  CHECK(session.current_patch().channel == defaults.channel);
  // DMP writes detune 4, "minus zero", as the 0 it sounds like.
  auto expected_instrument = defaults.instrument;
  for (auto &op : expected_instrument.operators) {
    if (op.detune == 4) {
      op.detune = 0;
    }
  }
  CHECK(session.current_patch().instrument == expected_instrument);

  // And it is open and selected exactly as a click in the browser leaves it.
  const auto relative =
      session.repository().to_relative_path(created.path).generic_string();
  CHECK(session.current_patch_path() == relative);
  CHECK(session.current_patch_selection_path() == relative);
  CHECK(!session.is_modified());
  CHECK(find_patch_entry(session.repository().tree(), created.path) != nullptr);

  // The name is taken in the format that claimed it, and free in any other.
  CHECK(session.create_patch_in(env.folder, "brand new", ".dmp").is_error());
  CHECK(session.create_patch_in(env.folder, "brand new", ".fui").is_success());

  // Names the filesystem would not take are refused before anything is
  // written, and nothing is left behind.
  CHECK(session.create_patch_in(env.folder, "", ".dmp").is_error());
  CHECK(session.create_patch_in(env.folder, "bad/name", ".dmp").is_error());
  CHECK(!std::filesystem::exists(env.folder / "bad"));

  // A folder nested inside a workspace folder is writable too.
  const auto nested = env.folder / "nested";
  std::filesystem::create_directories(nested);
  session.repository().refresh();
  CHECK(session.can_create_patch_in(nested));
  const auto in_nested = session.create_patch_in(nested, "deep", ".dmp");
  CHECK(in_nested.is_success());
  CHECK(in_nested.path == nested / "deep.dmp");
  CHECK(session.current_patch_selection_path() ==
        session.repository().to_relative_path(in_nested.path).generic_string());

  std::filesystem::remove(env.folder / "brand new.dmp");
  std::filesystem::remove(env.folder / "brand new.fui");
  std::filesystem::remove_all(nested);
  session.repository().refresh();
}

} // namespace

int main() {
  test_workspace_folder_is_visible();
  test_save_and_metadata_roundtrip();
  test_default_save_format_is_dmp();
  test_ginpkg_versions_appear_as_container_items();
  test_save_in_place_rules();
  test_create_patch_in_folder();
  std::cout << "All patch save tests passed\n";
  return 0;
}
