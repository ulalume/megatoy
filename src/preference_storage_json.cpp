#include "preference_storage.hpp"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {

class JsonPreferenceStorage : public PreferenceStorage {
public:
  explicit JsonPreferenceStorage(std::filesystem::path path)
      : path_(std::move(path)) {}

  bool load(PreferenceData &data) override {
    try {
      if (!std::filesystem::exists(path_)) {
        return true;
      }

      std::ifstream file(path_);
      if (!file) {
        std::cerr << "Failed to open preferences file for reading" << std::endl;
        return false;
      }

      nlohmann::json j;
      file >> j;

      if (j.contains("data_directory")) {
        data.data_directory = j["data_directory"].get<std::string>();
      }

      if (j.contains("theme")) {
        data.theme = ui::styles::theme_id_from_storage_key(
            j["theme"].get<std::string>(), data.theme);
      }

      if (j.contains("ui")) {
        const auto &ui = j["ui"];
        if (ui.contains("show_patch_editor")) {
          data.ui_preferences.show_patch_editor =
              ui["show_patch_editor"].get<bool>();
        }
        if (ui.contains("show_audio_controls")) {
          data.ui_preferences.show_audio_controls =
              ui["show_audio_controls"].get<bool>();
        }
        if (ui.contains("show_midi_keyboard")) {
          data.ui_preferences.show_midi_keyboard =
              ui["show_midi_keyboard"].get<bool>();
        }
        if (ui.contains("show_patch_selector")) {
          data.ui_preferences.show_patch_selector =
              ui["show_patch_selector"].get<bool>();
        }
        if (ui.contains("show_mml_console")) {
          data.ui_preferences.show_mml_console =
              ui["show_mml_console"].get<bool>();
        }
        if (ui.contains("show_wave_viewer")) {
          data.ui_preferences.show_waveform =
              ui["show_wave_viewer"].get<bool>();
        }
        if (ui.contains("show_preferences")) {
          data.ui_preferences.show_preferences =
              ui["show_preferences"].get<bool>();
        }
        if (ui.contains("patch_search_query")) {
          data.ui_preferences.patch_search_query =
              ui["patch_search_query"].get<std::string>();
        }
      }

      return true;
    } catch (const std::exception &e) {
      std::cerr << "Error loading preferences: " << e.what() << std::endl;
      return false;
    }
  }

  bool save(const PreferenceData &data) override {
    try {
      if (path_.has_parent_path()) {
        std::filesystem::create_directories(path_.parent_path());
      }

      nlohmann::json j;
      j["data_directory"] = data.data_directory.string();
      j["theme"] = ui::styles::storage_key(data.theme);

      nlohmann::json ui;
      ui["show_patch_editor"] = data.ui_preferences.show_patch_editor;
      ui["show_audio_controls"] = data.ui_preferences.show_audio_controls;
      ui["show_midi_keyboard"] = data.ui_preferences.show_midi_keyboard;
      ui["show_patch_selector"] = data.ui_preferences.show_patch_selector;
      ui["show_mml_console"] = data.ui_preferences.show_mml_console;
      ui["show_preferences"] = data.ui_preferences.show_preferences;
      ui["show_wave_viewer"] = data.ui_preferences.show_waveform;
      ui["patch_search_query"] = data.ui_preferences.patch_search_query;
      j["ui"] = ui;

      std::ofstream file(path_);
      if (!file) {
        std::cerr << "Failed to open preferences file for writing" << std::endl;
        return false;
      }

      file << j.dump(2);
      return true;
    } catch (const std::exception &e) {
      std::cerr << "Error saving preferences: " << e.what() << std::endl;
      return false;
    }
  }

private:
  std::filesystem::path path_;
};

} // namespace

std::unique_ptr<PreferenceStorage>
make_json_preference_storage(const std::filesystem::path &path) {
  return std::make_unique<JsonPreferenceStorage>(path);
}
