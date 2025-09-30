#include "app_state.hpp"
#include "patches/patch_repository.hpp"
#include "ui/preview/algorithm_preview.hpp"
#include "ui/preview/ssg_preview.hpp"
#include "ym2612/channel.hpp"
#include <algorithm>
#include <array>
#include <iostream>

AppState::PatchState::PatchState(const std::filesystem::path &preset_dir,
                                 const std::filesystem::path &user_dir)
    : current(), patch_repository(preset_dir) {}

AppState::AppState()
    : device_(), audio_manager_(), gui_manager_(), preference_manager_(),
      channel_allocator_(), input_state_(), ui_state_(), history_(),
      patch_state_(preference_manager_.get_patches_directory(),
                   preference_manager_.get_user_patches_directory()) {}

void AppState::init() {
  initialize_patch_defaults();
  wave_sampler_.clear();

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

bool AppState::key_on(ym2612::Note note, uint8_t velocity) {
  const uint8_t clamped_velocity =
      std::min<uint8_t>(velocity, static_cast<uint8_t>(0xFF));

  auto channel = channel_allocator_.note_on(note);
  if (!channel) {
    return false;
  }

  auto ym_channel = device_.channel(*channel);
  ym_channel.write_frequency(note);

  auto instrument = copy_instrument_with_velocity(
      patch_state_.current.instrument, clamped_velocity);
  ym_channel.write_instrument(instrument);

  ym_channel.write_key_on();

  std::cout << "Key ON - " << note << " (velocity "
            << static_cast<int>(clamped_velocity) << ")\n"
            << std::flush;
  return true;
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
  wave_sampler_.clear();

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

  ui_state_.prefs = preference_manager_.ui_preferences();
}

void AppState::configure_audio_callback() {
  audio_manager_.set_callback(
      [this](UINT32 sample_count, std::array<DEV_SMPL *, 2> &outputs) {
        device_.update(sample_count, outputs);
        wave_sampler_.push_samples(outputs[0], outputs[1], sample_count);
      });
}

uint8_t AppState::scale_total_level(uint8_t base_total_level,
                                    uint8_t velocity) {
  const uint16_t clamped_velocity =
      std::min<uint8_t>(velocity, static_cast<uint8_t>(127));
  const uint16_t reversed_total_level =
      127 - std::min<uint8_t>(base_total_level, static_cast<uint8_t>(127));

  const uint16_t scaled_velocity =
      127 - reversed_total_level * clamped_velocity / 127;
  std::cout << "base_total_level: " << static_cast<int>(base_total_level)
            << ", velocity: " << static_cast<int>(velocity)
            << ", scaled_velocity: " << static_cast<int>(scaled_velocity)
            << std::endl;
  return static_cast<uint8_t>(scaled_velocity);
}

ym2612::ChannelInstrument AppState::copy_instrument_with_velocity(
    const ym2612::ChannelInstrument &instrument, uint8_t velocity) const {
  ym2612::ChannelInstrument modified = instrument;
  std::cout << "is equal " << (modified == instrument) << std::endl;
  for (auto &op : modified.operators) {
    op.total_level = scale_total_level(op.total_level, velocity);
  }
  return modified;
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
