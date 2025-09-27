#pragma once

#include "audio_manager.hpp"
#include "channel_allocator.hpp"
#include "gui_manager.hpp"
#include "history/history_manager.hpp"
#include "patches/patch_repository.hpp"
#include "preference_manager.hpp"
#include "types.hpp"
#include "ym2612/device.hpp"
#include "ym2612/note.hpp"
#include "ym2612/patch.hpp"
#include <array>
#include <filesystem>
#include <string>

struct MidiKeyboardSettings {
  Key key = Key::C;
  Scale scale = Scale::CHROMATIC;
};

struct InputState {
  bool text_input_focused = false;
  uint8_t keyboard_typing_octave = 3;
  MidiKeyboardSettings midi_keyboard_settings;

  std::map<int, ym2612::Note> active_keyboard_notes;
};

struct UIState {
  bool show_patch_editor = true;
  bool show_audio_controls = true;
  bool show_midi_keyboard = true;
  bool show_preferences = false;
  bool show_patch_selector = true;
  std::string patch_search_query;
  bool open_directory_dialog = false;
};

class AppState {
public:
  AppState();

  void init();
  void shutdown();

  ym2612::Device &device() { return device_; }
  const ym2612::Device &device() const { return device_; }

  AudioManager &audio_manager() { return audio_manager_; }
  const AudioManager &audio_manager() const { return audio_manager_; }

  GuiManager &gui_manager() { return gui_manager_; }
  const GuiManager &gui_manager() const { return gui_manager_; }

  PreferenceManager &preference_manager() { return preference_manager_; }
  const PreferenceManager &preference_manager() const {
    return preference_manager_;
  }

  InputState &input_state() { return input_state_; }
  const InputState &input_state() const { return input_state_; }

  UIState &ui_state() { return ui_state_; }
  const UIState &ui_state() const { return ui_state_; }

  ChannelAllocator &channel_allocator() { return channel_allocator_; }
  const ChannelAllocator &channel_allocator() const {
    return channel_allocator_;
  }

  ym2612::Patch &patch();
  const ym2612::Patch &patch() const;

  patches::PatchRepository &patch_repository();
  const patches::PatchRepository &patch_repository() const;

  history::HistoryManager &history() { return history_; }
  const history::HistoryManager &history() const { return history_; }

  void update_all_settings();
  void apply_patch_to_device();

  bool key_on(ym2612::Note note);
  bool key_off(ym2612::Note note);
  bool key_is_pressed(const ym2612::Note &note) const;

  const std::array<bool, 6> &active_channels() const;

  // bool load_user_patch(const std::string &patch_name);
  bool load_patch(const patches::PatchEntry &preset_info);

  void sync_patch_directories();
  void sync_imgui_ini_file();

  const std::string &current_patch_path() const;
  void update_current_patch_path(const std::filesystem::path &patch_path);

private:
  static constexpr UINT32 kSampleRate = 44100;

  ym2612::Device device_;
  AudioManager audio_manager_;
  GuiManager gui_manager_;
  PreferenceManager preference_manager_;
  ChannelAllocator channel_allocator_;
  InputState input_state_;
  UIState ui_state_;
  history::HistoryManager history_;

  struct PatchState {
    ym2612::Patch current;
    patches::PatchRepository patch_repository;
    std::string current_patch_path;

    PatchState(const std::filesystem::path &preset_dir,
               const std::filesystem::path &user_dir);
  } patch_state_;

  void initialize_patch_defaults();
  void configure_audio();
  void configure_gui();
  void configure_audio_callback();
};
