#include "preference_storage.hpp"

#include <ios>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {

class JsonPreferenceStorage : public PreferenceStorage {
public:
  JsonPreferenceStorage(std::filesystem::path path,
                        platform::VirtualFileSystem &vfs)
      : path_(std::move(path)), vfs_(vfs) {}

  bool load(PreferenceData &data) override {
    try {
      if (!std::filesystem::exists(path_)) {
        return true;
      }

      auto stream = vfs_.open_read(path_);
      if (!stream) {
        std::cerr << "Failed to open preferences file for reading" << std::endl;
        return false;
      }

      nlohmann::json j;
      (*stream) >> j;

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
        if (ui.contains("show_patch_lab")) {
          data.ui_preferences.show_patch_lab = ui["show_patch_lab"].get<bool>();
        }
        if (ui.contains("use_velocity")) {
          data.ui_preferences.use_velocity = ui["use_velocity"].get<bool>();
        }
        if (ui.contains("patch_search_query")) {
          data.ui_preferences.patch_search_query =
              ui["patch_search_query"].get<std::string>();
        }
        if (ui.contains("steal_oldest_note_when_full")) {
          data.ui_preferences.steal_oldest_note_when_full =
              ui["steal_oldest_note_when_full"].get<bool>();
        }
        if (ui.contains("midi_keyboard_scale")) {
          data.ui_preferences.midi_keyboard_scale =
              ui["midi_keyboard_scale"].get<int>();
        }
        if (ui.contains("midi_keyboard_key")) {
          data.ui_preferences.midi_keyboard_key =
              ui["midi_keyboard_key"].get<int>();
        }
        if (ui.contains("midi_keyboard_typing_octave")) {
          data.ui_preferences.midi_keyboard_typing_octave =
              ui["midi_keyboard_typing_octave"].get<int>();
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
      ui["show_patch_lab"] = data.ui_preferences.show_patch_lab;
      ui["show_wave_viewer"] = data.ui_preferences.show_waveform;
      ui["use_velocity"] = data.ui_preferences.use_velocity;
      ui["steal_oldest_note_when_full"] =
          data.ui_preferences.steal_oldest_note_when_full;
      ui["patch_search_query"] = data.ui_preferences.patch_search_query;
      ui["midi_keyboard_scale"] = data.ui_preferences.midi_keyboard_scale;
      ui["midi_keyboard_key"] = data.ui_preferences.midi_keyboard_key;
      ui["midi_keyboard_typing_octave"] =
          data.ui_preferences.midi_keyboard_typing_octave;
      j["ui"] = ui;

      auto stream = vfs_.open_write(path_, std::ios::binary | std::ios::trunc);
      if (!stream) {
        std::cerr << "Failed to open preferences file for writing" << std::endl;
        return false;
      }

      (*stream) << j.dump(2);
      return true;
    } catch (const std::exception &e) {
      std::cerr << "Error saving preferences: " << e.what() << std::endl;
      return false;
    }
  }

private:
  std::filesystem::path path_;
  platform::VirtualFileSystem &vfs_;
};

} // namespace

std::unique_ptr<PreferenceStorage>
make_json_preference_storage(const std::filesystem::path &path,
                             platform::VirtualFileSystem &vfs) {
  return std::make_unique<JsonPreferenceStorage>(path, vfs);
}
