#include "app_state.hpp"
#include "formats/dmp.hpp"
#include "history/snapshot_entry.hpp"
#include "parsers/patch_loader.hpp"
#include "patches/patch_repository.hpp"
#include "ui/preview/algorithm_preview.hpp"
#include "ui/preview/ssg_preview.hpp"
#include "ym2612/channel.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
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
  const auto before = capture_patch_snapshot();
  if (!patch_manager_.load_patch(patch_info)) {
    std::cerr << "Failed to load preset patch: " << patch_info.name
              << std::endl;
    return false;
  }

  apply_patch_to_device();
  const auto after = capture_patch_snapshot();
  record_patch_change("Load Patch: " + patch_info.name, before, after);
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
  const auto &paths = directory_service_.paths();
  const auto init_patch = paths.builtin_presets_root / "init.dmp";
  auto &patch = patch_manager_.current_patch();

  bool loaded_builtin = false;
  if (!paths.builtin_presets_root.empty() &&
      std::filesystem::exists(init_patch)) {
    if (ym2612::formats::dmp::read_file(init_patch, patch)) {
      patch_manager_.set_current_patch_path(init_patch);
      loaded_builtin = true;
    } else {
      std::cerr << "Failed to load builtin init patch: " << init_patch << "\n";
    }
  }

  if (!loaded_builtin) {
    patch.name = "init";

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
        .algorithm = 3,
        .operators =
            {
                {31, 0, 0, 5, 0, 48, 0, 1, 3, 0, false, false},
                {31, 0, 0, 5, 0, 24, 0, 1, 1, 0, false, false},
                {31, 0, 0, 5, 0, 36, 0, 1, 2, 0, false, false},
                {31, 0, 0, 5, 0, 12, 0, 1, 4, 0, false, false},
            },
    };
    patch_manager_.set_current_patch_path({});
  }

  history_.clear();
}

void AppState::handle_patch_file_drop(const std::filesystem::path &path) {
  auto &drop = ui_state_.drop_state;
  drop.error_message.clear();

  if (!std::filesystem::exists(path) ||
      !std::filesystem::is_regular_file(path)) {
    drop.error_message = "File not found: " + path.string();
    drop.show_error_popup = true;
    return;
  }

  auto apply_loaded_patch = [&](const ym2612::Patch &loaded,
                                const std::filesystem::path &src,
                                const std::string &label) {
    const auto before = capture_patch_snapshot();
    patch_manager_.current_patch() = loaded;
    patch_manager_.set_current_patch_path(src);
    apply_patch_to_device();
    const auto after = capture_patch_snapshot();
    record_patch_change(label, before, after);
    drop.instruments.clear();
    drop.pending_instruments_path.clear();
    drop.selected_instrument = 0;
    drop.show_error_popup = false;
    drop.error_message.clear();
  };

  const auto result = parsers::load_patch_from_file(path);
  switch (result.status) {
  case parsers::PatchLoadStatus::Success:
    apply_loaded_patch(result.patch, path,
                       "Load Patch: " + path.filename().string());
    break;
  case parsers::PatchLoadStatus::MultiInstrument:
    drop.instruments = result.instruments;
    drop.pending_instruments_path = path;
    drop.selected_instrument = 0;
    drop.show_picker_for_multiple_instruments = true;
    drop.show_error_popup = false;
    drop.error_message.clear();
    break;
  case parsers::PatchLoadStatus::Failure:
  default:
    drop.instruments.clear();
    drop.pending_instruments_path.clear();
    drop.error_message = result.message.empty() ? "Unsupported file format: " +
                                                      path.filename().string()
                                                : result.message;
    drop.show_error_popup = true;
    drop.show_picker_for_multiple_instruments = false;
    break;
  }
}

void AppState::apply_mml_instrument_selection(size_t index) {
  auto &drop = ui_state_.drop_state;
  if (index >= drop.instruments.size()) {
    cancel_instrument_selection();
    return;
  }

  const auto before = capture_patch_snapshot();
  ym2612::Patch selected = drop.instruments[index].patch;
  if (!drop.instruments[index].name.empty()) {
    selected.name = drop.instruments[index].name;
  } else if (selected.name.empty()) {
    selected.name = drop.pending_instruments_path.stem().string();
  }

  patch_manager_.current_patch() = selected;
  patch_manager_.set_current_patch_path(drop.pending_instruments_path);
  apply_patch_to_device();
  const auto after = capture_patch_snapshot();
  std::string label = "Select Instrument";
  if (!drop.instruments[index].name.empty()) {
    label += ": " + drop.instruments[index].name;
  }
  record_patch_change(label, before, after);

  drop.instruments.clear();
  drop.pending_instruments_path.clear();
  drop.selected_instrument = 0;
  drop.show_picker_for_multiple_instruments = false;
  drop.error_message.clear();
  drop.show_error_popup = false;
}

void AppState::cancel_instrument_selection() {
  auto &drop = ui_state_.drop_state;
  drop.instruments.clear();
  drop.pending_instruments_path.clear();
  drop.selected_instrument = 0;
  drop.show_picker_for_multiple_instruments = false;
  drop.error_message.clear();
  drop.show_error_popup = false;
}

AppState::PatchSnapshot AppState::capture_patch_snapshot() const {
  PatchSnapshot snapshot;
  snapshot.patch = patch_manager_.current_patch();
  snapshot.path = patch_manager_.current_patch_path();
  return snapshot;
}

void AppState::apply_patch_snapshot(const PatchSnapshot &snapshot) {
  patch_manager_.current_patch() = snapshot.patch;
  if (snapshot.path.empty()) {
    patch_manager_.set_current_patch_path({});
  } else {
    patch_manager_.set_current_patch_path(snapshot.path);
  }
  apply_patch_to_device();
}

void AppState::record_patch_change(const std::string &label,
                                   const PatchSnapshot &before,
                                   const PatchSnapshot &after) {
  history_.begin_transaction(label, {}, [label, before, after](AppState &) {
    return history::make_snapshot_entry<PatchSnapshot>(
        label, std::string{}, before, after,
        [](AppState &state, const PatchSnapshot &snapshot) {
          state.apply_patch_snapshot(snapshot);
        });
  });
  history_.commit_transaction(*this);
}

void AppState::configure_audio() {
  const bool audio_ready = audio_manager_.init(kSampleRate);
  const UINT32 device_sample_rate =
      audio_ready ? audio_manager_.get_sample_rate() : kSampleRate;

  device_.stop();
  device_.init(device_sample_rate);
  wave_sampler_.clear();

  apply_patch_to_device();

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
