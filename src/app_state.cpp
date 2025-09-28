#include "app_state.hpp"
#include "patches/patch_repository.hpp"
#include "ui/preview/algorithm_preview.hpp"
#include "ui/preview/ssg_preview.hpp"
#include "ym2612/channel.hpp"
#include <array>
#include <iostream>

AppState::PatchState::PatchState(const std::filesystem::path &preset_dir,
                                 const std::filesystem::path &user_dir)
    : current(), patch_repository(preset_dir) {}

AppState::AppState()
    : device_(), audio_manager_(), gui_manager_(), preference_manager_(),
      channel_allocator_(), input_state_(), ui_state_(), history_(),
      patch_state_(preference_manager_.get_patches_directory(),
                   preference_manager_.get_user_patches_directory()) {
  const auto &ui_prefs = preference_manager_.ui_preferences();
}

void AppState::init() {
  initialize_patch_defaults();

  configure_audio();
  configure_gui();
}

void AppState::shutdown() {
  channel_allocator_.release_all(device_);

  audio_manager_.stop();
  audio_manager_.clear_callback();
  device_.stop();
  ui::reset_algorithm_preview_textures();
  ui::reset_ssg_preview_textures();
  gui_manager_.shutdown();
}

ym2612::Patch &AppState::patch() { return patch_state_.current; }

const ym2612::Patch &AppState::patch() const { return patch_state_.current; }

patches::PatchRepository &AppState::patch_repository() {
  return patch_state_.patch_repository;
}

const patches::PatchRepository &AppState::patch_repository() const {
  return patch_state_.patch_repository;
}

void AppState::update_all_settings() { apply_patch_to_device(); }

bool AppState::key_on(ym2612::Note note) {
  if (channel_allocator_.note_on(note, device_)) {
    std::cout << "Key ON - " << note << "\n" << std::flush;
    return true;
  }
  return false;
}

bool AppState::key_off(ym2612::Note note) {
  if (channel_allocator_.note_off(note, device_)) {
    std::cout << "Key OFF - " << note << "\n" << std::flush;
    return true;
  }
  return false;
}

bool AppState::key_is_pressed(const ym2612::Note &note) const {
  return channel_allocator_.is_note_active(note);
}

const std::array<bool, 6> &AppState::active_channels() const {
  return channel_allocator_.channel_usage();
}

bool AppState::load_patch(const patches::PatchEntry &patch_info) {
  ym2612::Patch loaded_patch;
  if (!patch_state_.patch_repository.load_patch(patch_info, loaded_patch)) {
    std::cerr << "Failed to load preset patch: " << patch_info.name
              << std::endl;
    return false;
  }

  patch_state_.current = loaded_patch;
  patch_state_.current_patch_path = patch_info.relative_path;
  apply_patch_to_device();
  history_.reset();
  std::cout << "Loaded preset patch: " << patch_info.name << std::endl;
  return true;
}

void AppState::sync_patch_directories() {
  auto patch_dir = preference_manager_.get_patches_directory();
  // auto user_dir = preference_manager_.get_user_patches_directory();

  // patch_state_.user_repository.set_directory(user_dir);
  patch_state_.patch_repository = patches::PatchRepository(patch_dir);
}

void AppState::sync_imgui_ini_file() {
  auto gui_ini_path = preference_manager_.get_imgui_ini_file();
  gui_manager_.set_imgui_ini_file(gui_ini_path.generic_string());
}

void AppState::initialize_patch_defaults() {
  auto &patch = patch_state_.current;
  patch.global = {
      .dac_enable = false,
      .lfo_enable = false,
      .lfo_frequency = 0,
  };

  patch.channel = {
      .left_speaker = true,
      .right_speaker = true,
      .amplitude_modulation_sensitivity = 0,
      .frequency_modulation_sensitivity = 0,
  };

  patch.instrument = {
      .feedback = 7,
      .algorithm = 5,
      .operators =
          {
              {15, 10, 0, 5, 2, 26, 0, 1, 3, 0, false, false},
              {21, 31, 0, 10, 0, 18, 0, 1, 3, 0, false, false},
              {21, 31, 0, 10, 0, 18, 0, 1, 3, 0, false, false},
              {21, 31, 0, 10, 0, 18, 0, 1, 3, 0, false, false},
          },
  };
}

void AppState::configure_audio() {
  const bool audio_ready = audio_manager_.init(kSampleRate);
  const UINT32 device_sample_rate =
      audio_ready ? audio_manager_.get_sample_rate() : kSampleRate;

  device_.stop();
  device_.init(device_sample_rate);

  apply_patch_to_device();
  history_.reset();

  if (!audio_ready) {
    std::cerr << "Failed to initialize audio system\n";
    audio_manager_.clear_callback();
    return;
  }

  if (device_sample_rate != kSampleRate) {
    std::cout << "Audio sample rate set to " << device_sample_rate << " Hz\n";
  }

  configure_audio_callback();

  if (!audio_manager_.start()) {
    std::cerr << "Failed to start audio streaming\n";
  }
}

void AppState::configure_gui() {
  gui_manager_.set_theme(preference_manager_.theme());
  if (!gui_manager_.init("megatoy", 1000, 700)) {
    std::cerr << "Failed to initialize GUI system\n";
  } else {
    sync_imgui_ini_file();
  }
  // Native dialogs require the GUI subsystem to be active on macOS.
  if (!preference_manager_.initialize_file_dialog()) {
    std::cerr << "Native File Dialog unavailable; directory picker disabled\n";
  }
}

void AppState::configure_audio_callback() {
  audio_manager_.set_callback(
      [this](UINT32 sample_count, std::array<DEV_SMPL *, 2> &outputs) {
        device_.update(sample_count, outputs);
      });
}

void AppState::apply_patch_to_device() {
  device_.write_settings(patch_state_.current.global);
  for (ym2612::ChannelIndex channel_index : ym2612::all_channel_indices) {
    device_.channel(channel_index).write_settings(patch_state_.current.channel);
    device_.channel(channel_index)
        .write_instrument(patch_state_.current.instrument);
  }
}

const std::string &AppState::current_patch_path() const {
  return patch_state_.current_patch_path;
}

void AppState::update_current_patch_path(
    const std::filesystem::path &patch_path) {
  std::cout << "Updating current patch path to: " << patch_path << std::endl;
  if (patch_path.empty()) {
    patch_state_.current_patch_path.clear();
  } else {
    patch_state_.current_patch_path = patch_path.generic_string();
  }
}
