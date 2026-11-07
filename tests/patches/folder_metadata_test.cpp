#include "../test_check.hpp"
#include "patches/folder_metadata.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>

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

void test_metadata_load_clamps_rating(const fs::path &root) {
  const auto sidecar = root / ".ym2612" / "patches.json";
  write_text(sidecar, R"({
  "version": 1,
  "patches": {
    "banks/Favorites.GINPKG/latest": {
      "star_rating": 9,
      "icon": "organ"
    }
  }
})");

  patches::FolderMetadataStore store(sidecar);
  CHECK(store.load());
  const auto latest = store.get("banks/Favorites.GINPKG/latest");
  CHECK(latest.has_value());
  CHECK(latest->path == "banks/Favorites.GINPKG/latest");
  CHECK(latest->star_rating == 5);
  CHECK(latest->icon == "organ");

  patches::FolderMetadataStore reloaded(sidecar);
  CHECK(reloaded.load());
  CHECK(reloaded.get("banks/Favorites.GINPKG/latest").has_value());
  fs::remove_all(root / ".ym2612");
}

void test_icon_is_written_and_read(const fs::path &root) {
  const auto sidecar = root / ".ym2612" / "patches.json";
  {
    patches::FolderMetadataStore store(sidecar);
    CHECK(store.load());
    patches::PatchMetadata metadata;
    metadata.path = "lead.dmp";
    metadata.icon = "organ";
    CHECK(store.put(metadata));
  }
  const auto written = nlohmann::json::parse(read_text(sidecar));
  CHECK(written["patches"]["lead.dmp"]["icon"] == "organ");
  CHECK(!written["patches"]["lead.dmp"].contains("category"));

  patches::FolderMetadataStore reloaded(sidecar);
  CHECK(reloaded.load());
  CHECK(reloaded.get("lead.dmp")->icon == "organ");
  fs::remove_all(root / ".ym2612");
}

void test_legacy_sidecar_fills_gaps(const fs::path &root) {
  const auto sidecar = root / ".ym2612" / "patches.json";
  const auto legacy = root / ".megatoy" / "patches.json";
  write_text(sidecar, R"({
  "patches": {
    "bass.dmp": {
      "gm": [39],
      "star_rating": 3
    }
  },
  "version": 1
})");
  write_text(legacy, R"({
  "version": 1,
  "patches": {
    "bass.dmp": {
      "star_rating": 5,
      "notes": "round",
      "category": "bass"
    },
    "lead.dmp": {
      "hash": "abc",
      "category": "lead"
    }
  }
})");

  patches::FolderMetadataStore store(sidecar, legacy);
  CHECK(store.load());
  const auto bass = store.get("bass.dmp");
  CHECK(bass.has_value());
  CHECK(bass->star_rating == 3);
  CHECK(bass->notes == "round");
  CHECK(bass->icon.empty());
  CHECK(!bass->extra.contains("category"));
  CHECK(bass->extra["gm"] == nlohmann::json::array({39}));
  const auto lead = store.get("lead.dmp");
  CHECK(lead.has_value());
  CHECK(lead->hash == "abc");
  CHECK(lead->icon.empty());
  CHECK(!lead->extra.contains("category"));
  // Loading alone leaves the old file in place.
  CHECK(fs::exists(legacy));

  patches::PatchMetadata pad;
  pad.path = "pad.dmp";
  pad.star_rating = 2;
  CHECK(store.put(pad));
  CHECK(!fs::exists(legacy));
  CHECK(!fs::exists(legacy.parent_path()));

  const auto written = nlohmann::json::parse(read_text(sidecar));
  const auto &entries = written["patches"];
  CHECK(entries.size() == 3);
  CHECK(entries["bass.dmp"]["star_rating"] == 3);
  CHECK(entries["bass.dmp"]["notes"] == "round");
  CHECK(entries["bass.dmp"]["gm"] == nlohmann::json::array({39}));
  CHECK(entries["lead.dmp"]["hash"] == "abc");
  for (const auto &[path, entry] : entries.items()) {
    CHECK(!entry.contains("category"));
  }
  fs::remove_all(root / ".ym2612");
}

void test_legacy_directory_with_other_files_stays(const fs::path &root) {
  const auto sidecar = root / ".ym2612" / "patches.json";
  const auto legacy = root / ".megatoy" / "patches.json";
  const auto other = root / ".megatoy" / "other.json";
  write_text(legacy, R"({"patches": {"a.dmp": {"star_rating": 4}}})");
  write_text(other, "{}");

  patches::FolderMetadataStore store(sidecar, legacy);
  CHECK(store.load());
  CHECK(store.get("a.dmp")->star_rating == 4);
  CHECK(store.remove("a.dmp"));
  CHECK(fs::exists(sidecar));
  CHECK(!fs::exists(legacy));
  CHECK(fs::exists(other));
  fs::remove_all(root / ".ym2612");
  fs::remove_all(root / ".megatoy");
}

void test_unknown_fields_survive_a_save(const fs::path &root) {
  const auto sidecar = root / ".ym2612" / "patches.json";
  const std::string original = R"({
  "patches": {
    "bass/sub.dmp": {
      "articulation": [
        "sustained"
      ],
      "gm": [
        39,
        40
      ],
      "icon": "synth-bass",
      "role": "bass",
      "timbre": [
        "dark"
      ]
    },
    "lead.dmp": {
      "star_rating": 2
    }
  },
  "tool": {
    "name": "other"
  },
  "version": 1
}
)";
  write_text(sidecar, original);

  patches::FolderMetadataStore store(sidecar);
  CHECK(store.load());
  auto bass = *store.get("bass/sub.dmp");
  CHECK(bass.icon == "synth-bass");
  bass.star_rating = 4;
  CHECK(store.put(bass));
  const auto stamped = *store.get("bass/sub.dmp");

  auto expected = nlohmann::json::parse(original);
  auto &entry = expected["patches"]["bass/sub.dmp"];
  entry["star_rating"] = 4;
  entry["created_at"] = stamped.created_at;
  entry["updated_at"] = stamped.updated_at;
  CHECK(read_text(sidecar) == expected.dump(2) + "\n");
  fs::remove_all(root / ".ym2612");
}

} // namespace

int main() {
  const auto root = fs::temp_directory_path() / "megatoy_folder_metadata_test";
  fs::remove_all(root);
  fs::create_directories(root);

  test_metadata_load_clamps_rating(root);
  test_icon_is_written_and_read(root);
  test_legacy_sidecar_fills_gaps(root);
  test_legacy_directory_with_other_files_stays(root);
  test_unknown_fields_survive_a_save(root);

  fs::remove_all(root);
  std::cout << "All folder metadata tests passed\n";
  return 0;
}
