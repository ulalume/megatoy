#include "../test_check.hpp"
#include "platform/std_file_system.hpp"
#include "preferences/preference_storage.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace {

nlohmann::json read_json(const fs::path &path) {
  std::ifstream stream(path);
  CHECK(stream.good());
  return nlohmann::json::parse(stream);
}

void check_fixture(const fs::path &fixtures, const fs::path &output,
                   const char *name, const char *normalized,
                   const PreferenceData &expected) {
  platform::StdFileSystem vfs;
  PreferenceData loaded;
  CHECK(make_json_preference_storage(fixtures / name, vfs)->load(loaded));
  CHECK(loaded.theme == expected.theme);
  CHECK(loaded.ui_preferences == expected.ui_preferences);
  CHECK(make_json_preference_storage(output, vfs)->save(loaded));
  CHECK(read_json(output) == read_json(fixtures / normalized));
}

/// Every saved value differs from its default, so a field that stops loading
/// comes back changed in the saved copy.
void check_every_field_survives(const fs::path &fixtures,
                                const fs::path &output) {
  const auto all = read_json(fixtures / "every_field.json");
  const auto defaults = read_json(fixtures / "default.json");
  CHECK(all.size() == defaults.size());
  for (const auto &[key, value] : all.items()) {
    if (key != "ui") {
      CHECK(value != defaults.at(key));
    }
  }
  CHECK(all.at("ui").size() == defaults.at("ui").size());
  for (const auto &[key, value] : all.at("ui").items()) {
    CHECK(value != defaults.at("ui").at(key));
  }

  platform::StdFileSystem vfs;
  PreferenceData loaded;
  CHECK(make_json_preference_storage(fixtures / "every_field.json", vfs)
            ->load(loaded));
  CHECK(make_json_preference_storage(output, vfs)->save(loaded));
  CHECK(read_json(output) == all);
}

void check_unknown_keys_are_ignored(const fs::path &fixtures,
                                    const fs::path &output) {
  const auto defaults = read_json(fixtures / "default.json");
  auto input = defaults;
  input["data_directory"] = "/unused";
  input["legacy_workspace_migration"] = 1;
  input["legacy_metadata_migration"] = 1;
  {
    std::ofstream stream(output);
    stream << input;
    CHECK(stream.good());
  }

  platform::StdFileSystem vfs;
  PreferenceData loaded;
  CHECK(make_json_preference_storage(output, vfs)->load(loaded));
  CHECK(make_json_preference_storage(output, vfs)->save(loaded));
  CHECK(read_json(output) == defaults);
}

} // namespace

int main(int argc, char **argv) {
  CHECK(argc == 2);
  const fs::path fixtures(argv[1]);
  const auto output = fs::temp_directory_path() /
                      "megatoy_preference_storage_compatibility.json";

  PreferenceData expected;
  check_fixture(fixtures, output, "default.json", "default.json", expected);
  check_fixture(fixtures, output, "terminated.json", "default.json", expected);

  expected.theme = ui::styles::ThemeId::MegatoyLight;
  expected.ui_preferences.show_waveform = false;
  expected.ui_preferences.ui_scale = 1.5f;
  expected.ui_preferences.midi_keyboard_layout = 2;
  expected.ui_preferences.custom_typing_layout_keys = {571, 569, 548, 0};
  expected.ui_preferences.custom_typing_octave_down_key = 603;
  expected.ui_preferences.custom_typing_octave_up_key = 605;
  check_fixture(fixtures, output, "custom.json", "custom.json", expected);
  check_every_field_survives(fixtures, output);
  check_unknown_keys_are_ignored(fixtures, output);

  fs::remove(output);
}
