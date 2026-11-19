#include "audio/load_meter.hpp"
#include "platform/host.hpp"
#include "preference_storage.hpp"
#include <algorithm>
#include <ios>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {

/// `target` takes the `T` stored under `key`, when there is one.
template <class T, class Target>
void read(const nlohmann::json &j, const char *key, Target &target) {
  if (j.contains(key)) {
    target = j[key].get<T>();
  }
}

/// A keyboard's custom keys, as the object stored under `key`.
void read_typing_keys(const nlohmann::json &j, const char *key,
                      ui::TypingKeysPreference &keys) {
  if (!j.contains(key) || !j[key].is_object()) {
    return;
  }
  const auto &stored = j[key];
  if (stored.contains("places") && stored["places"].is_array()) {
    stored["places"].get_to(keys.places);
  }
  read<int>(stored, "octave_down", keys.octave_down);
  read<int>(stored, "octave_up", keys.octave_up);
}

nlohmann::json typing_keys_json(const ui::TypingKeysPreference &keys) {
  return {{"places", keys.places},
          {"octave_down", keys.octave_down},
          {"octave_up", keys.octave_up}};
}

class JsonPreferenceStorage : public PreferenceStorage {
public:
  JsonPreferenceStorage(std::filesystem::path path,
                        platform::StdFileSystem &vfs)
      : path_(std::move(path)), vfs_(vfs) {}

  bool load(PreferenceData &data) override {
    try {
      nlohmann::json j;
      if (platform::host::keeps_settings()) {
        const auto stored = platform::host::read_settings();
        if (!stored.has_value()) {
          return true;
        }
        j = nlohmann::json::parse(stored.value());
      } else {
        if (!std::filesystem::exists(path_)) {
          return true;
        }

        auto stream = vfs_.open_read(path_);
        if (!stream) {
          std::cerr << "Failed to open preferences file for reading"
                    << std::endl;
          return false;
        }

        (*stream) >> j;
      }

      const bool has_workspace_folders =
          j.contains("workspace_folders") && j["workspace_folders"].is_array();
      if (has_workspace_folders) {
        data.workspace_folders.clear();
        for (const auto &entry : j["workspace_folders"]) {
          if (entry.is_string()) {
            data.workspace_folders.emplace_back(entry.get<std::string>());
          }
        }
      }

      read<std::string>(j, "last_save_directory", data.last_save_directory);
      read<std::string>(j, "last_patch_path", data.last_patch_path);
      if (j.contains("open_patches") && j["open_patches"].is_array()) {
        data.open_patches.clear();
        for (const auto &item : j["open_patches"]) {
          if (!item.is_object() || !item.contains("path") ||
              !item["path"].is_string()) {
            continue;
          }
          OpenPatchRecord record;
          record.path = item["path"].get<std::string>();
          if (item.contains("pinned") && item["pinned"].is_boolean()) {
            record.pinned = item["pinned"].get<bool>();
          }
          data.open_patches.push_back(std::move(record));
        }
      }
      if (j.contains("active_open_patch") &&
          j["active_open_patch"].is_number_integer()) {
        data.active_open_patch = j["active_open_patch"].get<int>();
      }
      read<std::string>(j, "last_seen_version", data.last_seen_version);
      read<bool>(j, "show_builtin_presets", data.show_builtin_presets);

      if (j.contains("theme")) {
        data.theme = ui::styles::theme_id_from_storage_key(
            j["theme"].get<std::string>(), data.theme);
      }

      if (j.contains("ui")) {
        const auto &ui = j["ui"];
        read<bool>(ui, "show_midi_keyboard",
                   data.ui_preferences.show_midi_keyboard);
        read<bool>(ui, "show_patch_selector",
                   data.ui_preferences.show_patch_selector);
        read<bool>(ui, "show_wave_viewer", data.ui_preferences.show_waveform);
        read<bool>(ui, "show_preferences",
                   data.ui_preferences.show_preferences);
        read<bool>(ui, "show_patch_lab", data.ui_preferences.show_patch_lab);
        read<bool>(ui, "show_quick_edit", data.ui_preferences.show_quick_edit);
        read<bool>(ui, "show_mml", data.ui_preferences.show_mml);
        if (ui.contains("ym2612_chip_type")) {
          data.ui_preferences.ym2612_chip_type =
              std::clamp(ui["ym2612_chip_type"].get<int>(), 0, 1);
        }
        if (ui.contains("ym2612_core")) {
          data.ui_preferences.ym2612_core =
              std::clamp(ui["ym2612_core"].get<int>(), 0, 1);
        }
        read<int>(ui, "audio_buffer_frames",
                  data.ui_preferences.audio_buffer_frames);
        if (ui.contains("audio_load_reading")) {
          data.ui_preferences.audio_load_reading =
              static_cast<int>(audio::load_reading_from_int(
                  ui["audio_load_reading"].get<int>()));
        }
        read<float>(ui, "ui_scale", data.ui_preferences.ui_scale);
        read<bool>(ui, "use_velocity", data.ui_preferences.use_velocity);
        read<bool>(ui, "use_pitch_bend", data.ui_preferences.use_pitch_bend);
        read<bool>(ui, "use_mod_wheel", data.ui_preferences.use_mod_wheel);
        if (ui.contains("velocity_sensitivity_depth")) {
          data.ui_preferences.velocity_sensitivity_depth =
              std::clamp(ui["velocity_sensitivity_depth"].get<int>(), 0, 100);
        }
        read<bool>(ui, "steal_oldest_note_when_full",
                   data.ui_preferences.steal_oldest_note_when_full);
        read<bool>(ui, "multi_operator_edit_absolute",
                   data.ui_preferences.multi_operator_edit_absolute);
        read<int>(ui, "envelope_reference_midi_note",
                  data.ui_preferences.envelope_reference_midi_note);
        read<int>(ui, "midi_keyboard_scale",
                  data.ui_preferences.midi_keyboard_scale);
        read<int>(ui, "midi_keyboard_key",
                  data.ui_preferences.midi_keyboard_key);
        read<int>(ui, "midi_keyboard_typing_octave",
                  data.ui_preferences.midi_keyboard_typing_octave);
        read<int>(ui, "midi_keyboard_typing_upper_octave",
                  data.ui_preferences.midi_keyboard_typing_upper_octave);
        read<int>(ui, "midi_keyboard_layout",
                  data.ui_preferences.midi_keyboard_layout);
        read<bool>(ui, "midi_keyboard_custom_keys",
                   data.ui_preferences.midi_keyboard_custom_keys);
        if (data.ui_preferences.midi_keyboard_layout ==
            ui::typing_layout_once_custom) {
          data.ui_preferences.midi_keyboard_layout =
              static_cast<int>(ui::TypingKeyboardLayout::OneKeyboard);
          data.ui_preferences.midi_keyboard_custom_keys = true;
        }
        read_typing_keys(ui, "custom_typing_keys",
                         data.ui_preferences.custom_typing_keys);
        read_typing_keys(ui, "custom_typing_lower_keys",
                         data.ui_preferences.custom_typing_lower_keys);
        read_typing_keys(ui, "custom_typing_upper_keys",
                         data.ui_preferences.custom_typing_upper_keys);
        if (ui.contains("custom_typing_layout_keys") &&
            ui["custom_typing_layout_keys"].is_array()) {
          ui::LegacyTypingKeys legacy;
          ui["custom_typing_layout_keys"].get_to(legacy.layout);
          // The built-in layout was once kept with a key for none at its end.
          if (!legacy.layout.empty() &&
              legacy.layout.back() == ui::typing_key_none &&
              legacy.layout.size() == ui::typing_layout_builtin_entry_count) {
            legacy.layout.pop_back();
          }
          read<int>(ui, "custom_typing_octave_down_key", legacy.octave_down);
          read<int>(ui, "custom_typing_octave_up_key", legacy.octave_up);
          if (!legacy.untouched()) {
            data.ui_preferences.legacy_custom_typing_keys = std::move(legacy);
          }
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
      nlohmann::json folders = nlohmann::json::array();
      for (const auto &folder : data.workspace_folders) {
        folders.push_back(folder.string());
      }
      j["workspace_folders"] = std::move(folders);
      j["last_save_directory"] = data.last_save_directory.string();
      j["last_patch_path"] = data.last_patch_path;
      nlohmann::json open_patches = nlohmann::json::array();
      for (const auto &record : data.open_patches) {
        open_patches.push_back(
            {{"path", record.path}, {"pinned", record.pinned}});
      }
      j["open_patches"] = std::move(open_patches);
      j["active_open_patch"] = data.active_open_patch;
      j["last_seen_version"] = data.last_seen_version;
      j["show_builtin_presets"] = data.show_builtin_presets;
      j["theme"] = ui::styles::storage_key(data.theme);

      nlohmann::json ui;
      ui["show_midi_keyboard"] = data.ui_preferences.show_midi_keyboard;
      ui["show_patch_selector"] = data.ui_preferences.show_patch_selector;
      ui["show_preferences"] = data.ui_preferences.show_preferences;
      ui["show_patch_lab"] = data.ui_preferences.show_patch_lab;
      ui["show_quick_edit"] = data.ui_preferences.show_quick_edit;
      ui["show_mml"] = data.ui_preferences.show_mml;
      ui["show_wave_viewer"] = data.ui_preferences.show_waveform;
      ui["ym2612_chip_type"] = data.ui_preferences.ym2612_chip_type;
      ui["ym2612_core"] = data.ui_preferences.ym2612_core;
      ui["audio_buffer_frames"] = data.ui_preferences.audio_buffer_frames;
      ui["audio_load_reading"] = data.ui_preferences.audio_load_reading;
      ui["ui_scale"] = data.ui_preferences.ui_scale;
      ui["use_velocity"] = data.ui_preferences.use_velocity;
      ui["use_pitch_bend"] = data.ui_preferences.use_pitch_bend;
      ui["use_mod_wheel"] = data.ui_preferences.use_mod_wheel;
      ui["velocity_sensitivity_depth"] =
          data.ui_preferences.velocity_sensitivity_depth;
      ui["steal_oldest_note_when_full"] =
          data.ui_preferences.steal_oldest_note_when_full;
      ui["multi_operator_edit_absolute"] =
          data.ui_preferences.multi_operator_edit_absolute;
      ui["envelope_reference_midi_note"] =
          data.ui_preferences.envelope_reference_midi_note;
      ui["midi_keyboard_scale"] = data.ui_preferences.midi_keyboard_scale;
      ui["midi_keyboard_key"] = data.ui_preferences.midi_keyboard_key;
      ui["midi_keyboard_typing_octave"] =
          data.ui_preferences.midi_keyboard_typing_octave;
      ui["midi_keyboard_typing_upper_octave"] =
          data.ui_preferences.midi_keyboard_typing_upper_octave;
      ui["midi_keyboard_layout"] = data.ui_preferences.midi_keyboard_layout;
      ui["midi_keyboard_custom_keys"] =
          data.ui_preferences.midi_keyboard_custom_keys;
      ui["custom_typing_keys"] =
          typing_keys_json(data.ui_preferences.custom_typing_keys);
      ui["custom_typing_lower_keys"] =
          typing_keys_json(data.ui_preferences.custom_typing_lower_keys);
      ui["custom_typing_upper_keys"] =
          typing_keys_json(data.ui_preferences.custom_typing_upper_keys);
      if (const auto &legacy = data.ui_preferences.legacy_custom_typing_keys) {
        ui["custom_typing_layout_keys"] = legacy->layout;
        ui["custom_typing_octave_down_key"] = legacy->octave_down;
        ui["custom_typing_octave_up_key"] = legacy->octave_up;
      }
      j["ui"] = ui;

      if (platform::host::keeps_settings()) {
        platform::host::write_settings(j.dump(2));
      } else {
        auto stream =
            vfs_.open_write(path_, std::ios::binary | std::ios::trunc);
        if (!stream) {
          std::cerr << "Failed to open preferences file for writing"
                    << std::endl;
          return false;
        }
        (*stream) << j.dump(2);
      }
      return true;
    } catch (const std::exception &e) {
      std::cerr << "Error saving preferences: " << e.what() << std::endl;
      return false;
    }
  }

private:
  std::filesystem::path path_;
  platform::StdFileSystem &vfs_;
};

} // namespace

std::unique_ptr<PreferenceStorage>
make_json_preference_storage(const std::filesystem::path &path,
                             platform::StdFileSystem &vfs) {
  return std::make_unique<JsonPreferenceStorage>(path, vfs);
}
