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
#include "ym2612/wave_sampler.hpp"
#include <array>
#include <filesystem>
#include <string>
#include <vector>

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
  PreferenceManager::UIPreferences prefs;
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

  ym2612::WaveSampler &wave_sampler() { return wave_sampler_; }
  const ym2612::WaveSampler &wave_sampler() const { return wave_sampler_; }

  void update_all_settings();
  void apply_patch_to_device();

  bool key_on(ym2612::Note note, uint8_t velocity);
  bool key_on(ym2612::Note note) { return key_on(note, 127); }
  bool key_off(ym2612::Note note);
  bool key_is_pressed(const ym2612::Note &note) const;

  const std::array<bool, 6> &active_channels() const;
  const std::vector<std::string> &connected_midi_inputs() const {
    return connected_midi_inputs_;
  }
  void set_connected_midi_inputs(std::vector<std::string> devices);

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
  ym2612::WaveSampler wave_sampler_;
  std::vector<std::string> connected_midi_inputs_;

  struct PatchState {
    ym2612::Patch current;
    patches::PatchRepository patch_repository;
    std::string current_patch_path;

    PatchState(const std::filesystem::path &user_dir,
               const std::filesystem::path &builtin_dir);
  } patch_state_;

  void initialize_patch_defaults();
  void configure_audio();
  void configure_gui();
  void configure_audio_callback();

  static uint8_t scale_total_level(uint8_t base_total_level, uint8_t velocity);
};
