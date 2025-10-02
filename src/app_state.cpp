#include "app_state.hpp"
#include "patches/patch_repository.hpp"
#include "ui/preview/algorithm_preview.hpp"
#include "ui/preview/ssg_preview.hpp"
#include "ym2612/channel.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <utility>

AppState::AppState()
    : device_(), audio_manager_(), gui_manager_(), directory_service_(),
      preference_manager_(directory_service_),
      patch_manager_(directory_service_), channel_allocator_(), input_state_(),
      ui_state_(), history_() {}

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

ym2612::Patch &AppState::patch() { return patch_manager_.current_patch(); }
const ym2612::Patch &AppState::patch() const {
  return patch_manager_.current_patch();
}

patches::PatchRepository &AppState::patch_repository() {
  return patch_manager_.repository();
}

const patches::PatchRepository &AppState::patch_repository() const {
  return patch_manager_.repository();
}

void AppState::update_all_settings() { apply_patch_to_device(); }

bool AppState::key_on(ym2612::Note note, uint8_t velocity) {
  const uint8_t clamped_velocity =
      std::min<uint8_t>(velocity, static_cast<uint8_t>(127));
  const uint8_t effective_velocity = ui_state_.prefs.use_velocity
                                         ? clamped_velocity
                                         : static_cast<uint8_t>(127);

  auto channel = channel_allocator_.note_on(note);
  if (!channel) {
    return false;
  }

  auto ym_channel = device_.channel(*channel);
  ym_channel.write_frequency(note);

  auto instrument =
      patch_manager_.current_patch().instrument.clone_with_velocity(
          effective_velocity);
  ym_channel.write_instrument(instrument);
  ym_channel.write_key_on();
  std::cout << "Key ON - " << note << " (velocity "
            << static_cast<int>(effective_velocity) << ")\n"
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

void AppState::set_connected_midi_inputs(std::vector<std::string> devices) {
  connected_midi_inputs_ = std::move(devices);
}

bool AppState::load_patch(const patches::PatchEntry &patch_info) {
  if (!patch_manager_.load_patch(patch_info)) {
    std::cerr << "Failed to load preset patch: " << patch_info.name
              << std::endl;
    return false;
  }

  apply_patch_to_device();
  history_.reset();
  std::cout << "Loaded preset patch: " << patch_info.name << std::endl;
  return true;
}

void AppState::sync_patch_directories() {
  directory_service_.ensure_directories();
  patch_manager_.refresh_directories();
}

void AppState::sync_imgui_ini_file() {
  auto gui_ini_path = preference_manager_.get_imgui_ini_file();
  gui_manager_.set_imgui_ini_file(gui_ini_path.generic_string());
}

void AppState::initialize_patch_defaults() {
  auto &patch = patch_manager_.current_patch();
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

  patch_manager_.set_current_patch_path({});
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

void AppState::apply_patch_to_device() {
  device_.write_settings(patch_manager_.current_patch().global);
  for (ym2612::ChannelIndex channel_index : ym2612::all_channel_indices) {
    device_.channel(channel_index)
        .write_settings(patch_manager_.current_patch().channel);
    device_.channel(channel_index)
        .write_instrument(patch_manager_.current_patch().instrument);
  }
}
