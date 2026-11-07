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

constexpr const char *kDefaults = R"({
  "patches": {
    "bass/sub.dmp": {
      "gm": [
        39
      ],
      "icon": "synth-bass",
      "role": "bass"
    },
    "keys/organ.dmp": {
      "icon": "organ",
      "star_rating": 4
    }
  },
  "version": 1
}
)";

void test_defaults_visible_without_user_file(const fs::path &root) {
  const auto user = root / "user" / "builtin.json";
  const auto defaults = root / "bundle" / ".ym2612" / "patches.json";
  write_text(defaults, kDefaults);

  patches::FolderMetadataStore store(user, {}, defaults);
  CHECK(store.load());
  const auto bass = store.get("bass/sub.dmp");
  CHECK(bass.has_value());
  CHECK(bass->icon == "synth-bass");
  CHECK(bass->extra["role"] == "bass");
  CHECK(store.get("keys/organ.dmp")->star_rating == 4);
  CHECK(store.entries().size() == 2);
  CHECK(!fs::exists(user));
  CHECK(read_text(defaults) == kDefaults);
  fs::remove_all(root / "user");
  fs::remove_all(root / "bundle");
}

void test_user_fields_override_defaults(const fs::path &root) {
  const auto user = root / "user" / "builtin.json";
  const auto defaults = root / "bundle" / ".ym2612" / "patches.json";
  write_text(defaults, kDefaults);
  write_text(user, R"({
  "patches": {
    "bass/sub.dmp": {
      "icon": "bass",
      "star_rating": 2
    },
    "lead.dmp": {
      "icon": "flute"
    }
  },
  "version": 1
}
)");

  patches::FolderMetadataStore store(user, {}, defaults);
  CHECK(store.load());
  const auto bass = store.get("bass/sub.dmp");
  CHECK(bass->icon == "bass");
  CHECK(bass->star_rating == 2);
  CHECK(bass->extra["gm"] == nlohmann::json::array({39}));
  CHECK(store.get("lead.dmp")->icon == "flute");
  CHECK(store.get("keys/organ.dmp")->icon == "organ");
  CHECK(store.entries().size() == 3);
  fs::remove_all(root / "user");
  fs::remove_all(root / "bundle");
}

void test_save_writes_only_differences(const fs::path &root) {
  const auto user = root / "user" / "builtin.json";
  const auto defaults = root / "bundle" / ".ym2612" / "patches.json";
  write_text(defaults, kDefaults);

  patches::FolderMetadataStore store(user, {}, defaults);
  CHECK(store.load());
  auto bass = *store.get("bass/sub.dmp");
  bass.star_rating = 3;
  CHECK(store.put(bass));

  const auto written = nlohmann::json::parse(read_text(user));
  const auto &patches = written["patches"];
  CHECK(patches.size() == 1);
  const auto &entry = patches["bass/sub.dmp"];
  CHECK(entry["star_rating"] == 3);
  CHECK(!entry.contains("icon"));
  CHECK(!entry.contains("gm"));
  CHECK(!entry.contains("role"));
  CHECK(entry.contains("created_at"));
  CHECK(entry.contains("updated_at"));

  // Back to what the defaults say: nothing but timestamps is left.
  bass = *store.get("bass/sub.dmp");
  bass.star_rating = 0;
  CHECK(store.put(bass));
  const auto reverted = nlohmann::json::parse(read_text(user));
  CHECK(reverted["patches"].empty());
  CHECK(read_text(defaults) == kDefaults);
  fs::remove_all(root / "user");
  fs::remove_all(root / "bundle");
}

void test_clearing_a_defaulted_field_persists(const fs::path &root) {
  const auto user = root / "user" / "builtin.json";
  const auto defaults = root / "bundle" / ".ym2612" / "patches.json";
  write_text(defaults, kDefaults);
  {
    patches::FolderMetadataStore store(user, {}, defaults);
    CHECK(store.load());
    auto organ = *store.get("keys/organ.dmp");
    organ.icon.clear();
    organ.star_rating = 0;
    CHECK(store.put(organ));
  }
  const auto written = nlohmann::json::parse(read_text(user));
  CHECK(written["patches"]["keys/organ.dmp"]["icon"] == "");
  CHECK(written["patches"]["keys/organ.dmp"]["star_rating"] == 0);

  patches::FolderMetadataStore reloaded(user, {}, defaults);
  CHECK(reloaded.load());
  const auto organ = reloaded.get("keys/organ.dmp");
  CHECK(organ->icon.empty());
  CHECK(organ->star_rating == 0);
  CHECK(reloaded.get("bass/sub.dmp")->icon == "synth-bass");
  CHECK(read_text(defaults) == kDefaults);
  fs::remove_all(root / "user");
  fs::remove_all(root / "bundle");
}

void test_retain_only_leaves_defaults(const fs::path &root) {
  const auto user = root / "user" / "builtin.json";
  const auto defaults = root / "bundle" / ".ym2612" / "patches.json";
  write_text(defaults, kDefaults);

  patches::FolderMetadataStore store(user, {}, defaults);
  CHECK(store.load());
  CHECK(store.retain_only({"keys/organ.dmp"}));
  CHECK(!fs::exists(user));
  CHECK(!store.get("bass/sub.dmp").has_value());

  auto organ = *store.get("keys/organ.dmp");
  organ.icon = "pipe-organ";
  CHECK(store.put(organ));
  CHECK(store.retain_only({}));
  const auto written = nlohmann::json::parse(read_text(user));
  CHECK(written["patches"].empty());
  CHECK(read_text(defaults) == kDefaults);

  patches::FolderMetadataStore reloaded(user, {}, defaults);
  CHECK(reloaded.load());
  CHECK(reloaded.get("bass/sub.dmp")->icon == "synth-bass");
  CHECK(reloaded.get("keys/organ.dmp")->icon == "organ");
  fs::remove_all(root / "user");
  fs::remove_all(root / "bundle");
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
  test_defaults_visible_without_user_file(root);
  test_user_fields_override_defaults(root);
  test_save_writes_only_differences(root);
  test_clearing_a_defaulted_field_persists(root);
  test_retain_only_leaves_defaults(root);

  fs::remove_all(root);
  std::cout << "All folder metadata tests passed\n";
  return 0;
}
