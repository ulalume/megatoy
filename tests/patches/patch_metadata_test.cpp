#include "../test_check.hpp"
#include "patches/patch_metadata.hpp"
#include "patches/patch_repository.hpp"
#include "patches/patch_write.hpp"
#include "platform/std_file_system.hpp"
#include "workspace/workspace.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void write_text(const fs::path &path, const std::string &text) {
  fs::create_directories(path.parent_path());
  std::ofstream file(path);
  file << text;
}

std::string read_text(const fs::path &path) {
  std::ifstream file(path);
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

nlohmann::json read_json(const fs::path &path) {
  return nlohmann::json::parse(read_text(path));
}

void write_dmp(const fs::path &path) {
  fs::create_directories(path.parent_path());
  CHECK(patches::write_patch(ym2612::Patch{}, path));
}

const patches::PatchEntry *find_entry(const patches::PatchRepository &repo,
                                      const fs::path &full_path) {
  const auto search = [&](const auto &self,
                          const std::vector<patches::PatchEntry> &entries)
      -> const patches::PatchEntry * {
    for (const auto &entry : entries) {
      if (!entry.is_directory && entry.full_path == full_path) {
        return &entry;
      }
      if (const auto *found = self(self, entry.children)) {
        return found;
      }
    }
    return nullptr;
  };
  return search(search, repo.tree());
}

std::string relative_of(const patches::PatchRepository &repo,
                        const fs::path &path) {
  return repo.to_relative_path(path).generic_string();
}

void test_sidecar_paths() {
  CHECK(patches::sidecar::path_for("/lib/bass/sub.dmp") ==
        fs::path("/lib/bass/.ym2612/sub.dmp.json"));
  CHECK(patches::sidecar::path_for("/lib/bank.opm", "0_piano") ==
        fs::path("/lib/.ym2612/bank.opm/0_piano.json"));
}

void test_round_trip_keeps_unknown_fields(const fs::path &root) {
  const auto folder = root / "round_trip";
  const auto patch = folder / "bass" / "sub bass.dmp";
  write_dmp(patch);
  const auto sidecar = folder / "bass" / ".ym2612" / "sub bass.dmp.json";
  write_text(sidecar, R"({"icon": "synth-bass", "gm": [39], "role": "bass",
    "timbre": ["dark"]})");

  platform::StdFileSystem file_system;
  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(folder));
  patches::PatchRepository repository(file_system, workspace);
  const auto *entry = find_entry(repository, fs::canonical(patch));
  CHECK(entry != nullptr && entry->metadata.has_value());
  CHECK(entry->metadata->icon == "synth-bass");
  CHECK(entry->metadata->extra["role"] == "bass");

  auto metadata = *entry->metadata;
  metadata.star_rating = 3;
  CHECK(repository.update_patch_metadata(entry->relative_path, metadata));

  const auto text = read_text(sidecar);
  const auto written = nlohmann::json::parse(text);
  CHECK(text == written.dump(2) + "\n");
  CHECK(written["star_rating"] == 3);
  CHECK(written["icon"] == "synth-bass");
  CHECK(written["gm"] == nlohmann::json::array({39}));
  CHECK(written["role"] == "bass");
  CHECK(written["timbre"] == nlohmann::json::array({"dark"}));
  CHECK(written["created_at"].is_string());
  CHECK(written["updated_at"].is_string());
}

void test_wrong_types_are_ignored_and_kept(const fs::path &root) {
  const auto folder = root / "wrong_types";
  const auto patch = folder / "lead.dmp";
  write_dmp(patch);
  const auto sidecar = folder / ".ym2612" / "lead.dmp.json";
  write_text(
      sidecar,
      R"({"icon": 5, "star_rating": 9, "tags": ["a", 1], "notes": "n"})");

  platform::StdFileSystem file_system;
  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(folder));
  patches::PatchRepository repository(file_system, workspace);
  const auto *entry = find_entry(repository, fs::canonical(patch));
  CHECK(entry != nullptr && entry->metadata.has_value());
  CHECK(entry->metadata->icon.empty());
  CHECK(entry->metadata->star_rating == 0);
  CHECK(entry->metadata->tags.empty());
  CHECK(entry->metadata->notes == "n");

  auto metadata = *entry->metadata;
  metadata.icon = "organ";
  CHECK(repository.update_patch_metadata(entry->relative_path, metadata));
  const auto written = read_json(sidecar);
  CHECK(written["icon"] == "organ");
  CHECK(written["star_rating"] == 9);
  CHECK(written["tags"] == nlohmann::json::parse(R"(["a", 1])"));
  CHECK(written["notes"] == "n");
}

void test_unreadable_sidecar_affects_one_patch(const fs::path &root) {
  const auto folder = root / "unreadable";
  write_dmp(folder / "a.dmp");
  write_dmp(folder / "b.dmp");
  const auto broken = folder / ".ym2612" / "a.dmp.json";
  write_text(broken, "{not json");
  write_text(folder / ".ym2612" / "b.dmp.json", R"({"icon": "bell"})");

  platform::StdFileSystem file_system;
  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(folder));
  patches::PatchRepository repository(file_system, workspace);
  const auto canonical = fs::canonical(folder);
  const auto *a = find_entry(repository, canonical / "a.dmp");
  const auto *b = find_entry(repository, canonical / "b.dmp");
  CHECK(a != nullptr && !a->metadata.has_value());
  CHECK(b != nullptr && b->metadata.has_value() && b->metadata->icon == "bell");

  // Not written over: the file may be someone's to fix.
  patches::PatchMetadata metadata;
  metadata.star_rating = 2;
  CHECK(!repository.update_patch_metadata(a->relative_path, metadata));
  CHECK(read_text(broken) == "{not json");
}

void test_empty_entry_removes_file(const fs::path &root) {
  const auto folder = root / "empty";
  write_dmp(folder / "a.dmp");

  platform::StdFileSystem file_system;
  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(folder));
  patches::PatchRepository repository(file_system, workspace);
  const auto canonical = fs::canonical(folder);
  const auto relative = relative_of(repository, canonical / "a.dmp");

  patches::PatchMetadata metadata;
  metadata.star_rating = 4;
  metadata.hash = "abc";
  CHECK(repository.update_patch_metadata(relative, metadata));
  CHECK(fs::exists(canonical / ".ym2612" / "a.dmp.json"));

  // A hash and timestamps alone are not worth a file.
  metadata.star_rating = 0;
  CHECK(repository.update_patch_metadata(relative, metadata));
  CHECK(!fs::exists(canonical / ".ym2612" / "a.dmp.json"));
  CHECK(!fs::exists(canonical / ".ym2612"));
}

void test_migration(const fs::path &root) {
  const auto folder = root / "migrate";
  write_dmp(folder / "a.dmp");
  write_dmp(folder / "sub" / "b.dmp");
  write_text(folder / ".megatoy" / "patches.json", R"({
  "patches": {
    "a.dmp": {"star_rating": 2, "icon": "old", "category": "Lead"},
    "gone.dmp": {"star_rating": 5}
  },
  "version": 1
})");
  write_text(folder / ".ym2612" / "patches.json", R"({
  "patches": {
    "a.dmp": {"icon": "organ"},
    "sub/b.dmp": {"star_rating": 3, "role": "bass"}
  },
  "version": 1
})");
  write_text(folder / "sub" / ".ym2612" / "b.dmp.json",
             R"({"star_rating": 1})");

  platform::StdFileSystem file_system;
  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(folder));
  patches::PatchRepository repository(file_system, workspace);

  const auto a = read_json(folder / ".ym2612" / "a.dmp.json");
  CHECK(a["icon"] == "organ");
  CHECK(a["star_rating"] == 2);
  CHECK(!a.contains("category"));
  const auto b = read_json(folder / "sub" / ".ym2612" / "b.dmp.json");
  CHECK(b["star_rating"] == 1);
  CHECK(b["role"] == "bass");
  CHECK(!fs::exists(folder / ".ym2612" / "gone.dmp.json"));
  CHECK(!fs::exists(folder / ".ym2612" / "patches.json"));
  CHECK(!fs::exists(folder / ".megatoy"));

  const auto *entry = find_entry(repository, fs::canonical(folder) / "a.dmp");
  CHECK(entry != nullptr && entry->metadata.has_value());
  CHECK(entry->metadata->icon == "organ");
}

void test_unreadable_legacy_file_is_left(const fs::path &root) {
  const auto folder = root / "broken_legacy";
  write_dmp(folder / "a.dmp");
  const auto legacy = folder / ".ym2612" / "patches.json";
  write_text(legacy, "{broken");
  write_text(folder / ".ym2612" / "a.dmp.json", R"({"icon": "bass"})");

  platform::StdFileSystem file_system;
  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(folder));
  patches::PatchRepository repository(file_system, workspace);

  CHECK(read_text(legacy) == "{broken");
  const auto *entry = find_entry(repository, fs::canonical(folder) / "a.dmp");
  CHECK(entry != nullptr && entry->metadata.has_value());
  CHECK(entry->metadata->icon == "bass");
}

void test_builtin_base_layer(const fs::path &root) {
  const auto builtin = root / "builtin";
  const auto preset = builtin / "keys" / "piano.dmp";
  write_dmp(preset);
  const auto bundled = builtin / "keys" / ".ym2612" / "piano.dmp.json";
  const std::string bundled_text =
      R"({"icon": "grand-piano", "role": "keys", "star_rating": 2})";
  write_text(bundled, bundled_text);
  const auto stored = root / "config" / "builtin_presets.json";

  platform::StdFileSystem file_system;
  megatoy::workspace::Workspace workspace;
  const auto entry_in = [&](const patches::PatchRepository &repository) {
    const auto *entry = find_entry(repository, preset);
    CHECK(entry != nullptr && entry->metadata.has_value());
    return *entry;
  };
  const auto stored_entry = [&]() {
    const auto patches = read_json(stored)["patches"];
    return patches.contains("keys/piano.dmp") ? patches["keys/piano.dmp"]
                                              : nlohmann::json();
  };

  {
    patches::PatchRepository repository(file_system, workspace, builtin,
                                        stored);
    auto entry = entry_in(repository);
    CHECK(entry.metadata->icon == "grand-piano");
    CHECK(entry.metadata->star_rating == 2);
    CHECK(!fs::exists(stored));

    auto metadata = *entry.metadata;
    metadata.star_rating = 5;
    CHECK(repository.update_patch_metadata(entry.relative_path, metadata));
    const auto own = stored_entry();
    CHECK(own["star_rating"] == 5);
    CHECK(!own.contains("icon"));
    CHECK(!own.contains("role"));

    repository.refresh();
    entry = entry_in(repository);
    metadata = *entry.metadata;
    metadata.icon.clear();
    CHECK(repository.update_patch_metadata(entry.relative_path, metadata));
    CHECK(stored_entry()["icon"] == "");
  }

  {
    patches::PatchRepository repository(file_system, workspace, builtin,
                                        stored);
    const auto entry = entry_in(repository);
    CHECK(entry.metadata->star_rating == 5);
    CHECK(entry.metadata->icon.empty());
    CHECK(entry.metadata->extra["role"] == "keys");

    // Back to the bundle's values: nothing of the user's is left.
    auto metadata = *entry.metadata;
    metadata.icon = "grand-piano";
    metadata.star_rating = 2;
    CHECK(repository.update_patch_metadata(entry.relative_path, metadata));
    CHECK(stored_entry().is_null());
  }
  CHECK(read_text(bundled) == bundled_text);
}

void test_copy_takes_metadata_without_bookkeeping(const fs::path &root) {
  const auto folder = root / "copy";
  write_dmp(folder / "a.dmp");
  write_text(folder / ".ym2612" / "a.dmp.json",
             R"({"icon": "bell", "role": "keys", "hash": "abc",
                 "created_at": "2020-01-01T00:00:00Z"})");

  platform::StdFileSystem file_system;
  megatoy::workspace::Workspace workspace;
  CHECK(workspace.add(folder));
  patches::PatchRepository repository(file_system, workspace);
  const auto canonical = fs::canonical(folder);
  const auto *source = find_entry(repository, canonical / "a.dmp");
  CHECK(source != nullptr);
  const auto metadata = source->metadata;

  write_dmp(canonical / "b.dmp");
  repository.copy_patch_metadata(metadata, canonical / "b.dmp");
  const auto copied = read_json(canonical / ".ym2612" / "b.dmp.json");
  CHECK(copied["icon"] == "bell");
  CHECK(copied["role"] == "keys");
  CHECK(!copied.contains("hash"));
  CHECK(copied["created_at"] != "2020-01-01T00:00:00Z");

  // A copy of a patch with nothing recorded has nothing either.
  repository.copy_patch_metadata(std::nullopt, canonical / "b.dmp");
  CHECK(!fs::exists(canonical / ".ym2612" / "b.dmp.json"));
}

} // namespace

int main() {
  const auto temp = fs::temp_directory_path() / "megatoy_patch_metadata_test";
  fs::remove_all(temp);
  fs::create_directories(temp);
  const auto root = fs::canonical(temp);

  test_sidecar_paths();
  test_round_trip_keeps_unknown_fields(root);
  test_wrong_types_are_ignored_and_kept(root);
  test_unreadable_sidecar_affects_one_patch(root);
  test_empty_entry_removes_file(root);
  test_migration(root);
  test_unreadable_legacy_file_is_left(root);
  test_builtin_base_layer(root);
  test_copy_takes_metadata_without_bookkeeping(root);

  fs::remove_all(root);
  std::cout << "All patch metadata tests passed\n";
  return 0;
}
