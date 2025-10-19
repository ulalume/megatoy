#include "app_state.hpp"

#include "history/snapshot_entry.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>

AppState::AppState()
    : directory_service_(), preference_manager_(directory_service_),
      audio_manager_(), gui_manager_(preference_manager_),
      patch_session_(directory_service_, audio_manager_), input_state_(),
      ui_state_(), history_(), connected_midi_inputs_() {}

void AppState::init() {
  directory_service_.ensure_directories();
  patch_session_.initialize_patch_defaults();

  if (!audio_manager_.initialize(kSampleRate)) {
    std::cerr
        << "Audio manager failed to start; functionality will be limited\n";
  } else {
    patch_session_.apply_patch_to_audio();
  }

  if (!gui_manager_.initialize("megatoy", 1000, 700)) {
    std::cerr << "GUI manager failed to start; shutting down\n";
  } else {
    gui_manager_.sync_imgui_ini();
  }

  ui_state_.prefs = preference_manager_.ui_preferences();
  history_.clear();
}

void AppState::shutdown() {
  patch_session_.release_all_notes();
  audio_manager_.shutdown();
  gui_manager_.shutdown();
}

void AppState::update_all_settings() { patch_session_.apply_patch_to_audio(); }

bool AppState::key_on(ym2612::Note note, uint8_t velocity) {
  const bool success = patch_session_.note_on(note, velocity, ui_state_.prefs);
  if (success) {
    const uint8_t effective_velocity =
        ui_state_.prefs.use_velocity
            ? std::min<uint8_t>(velocity, static_cast<uint8_t>(127))
            : static_cast<uint8_t>(127);
    std::cout << "Key ON - " << note << " (velocity "
              << static_cast<int>(effective_velocity) << ")\n"
              << std::flush;
  }
  return success;
}

bool AppState::key_off(ym2612::Note note) {
  if (patch_session_.note_off(note)) {
    std::cout << "Key OFF - " << note << "\n" << std::flush;
    return true;
  }
  return false;
}

bool AppState::key_is_pressed(const ym2612::Note &note) const {
  return patch_session_.note_is_active(note);
}

const std::array<bool, 6> &AppState::active_channels() const {
  return patch_session_.active_channels();
}

void AppState::set_connected_midi_inputs(std::vector<std::string> devices) {
  connected_midi_inputs_ = std::move(devices);
}

bool AppState::load_patch(const patches::PatchEntry &patch_info) {
  const auto before = capture_patch_snapshot();
  if (!patch_session_.load_patch_no_history(patch_info)) {
    std::cerr << "Failed to load preset patch: " << patch_info.name
              << std::endl;
    return false;
  }

  const auto after = capture_patch_snapshot();
  record_patch_change("Load Patch: " + patch_info.name, before, after);
  std::cout << "Loaded preset patch: " << patch_info.name << std::endl;
  return true;
}

void AppState::sync_patch_directories() {
  directory_service_.ensure_directories();
  patch_session_.refresh_directories();
}

void AppState::sync_imgui_ini_file() { gui_manager_.sync_imgui_ini(); }

void AppState::handle_patch_file_drop(const std::filesystem::path &path) {
  auto &drop = ui_state_.drop_state;
  drop.error_message.clear();

  const auto result = patch_session_.load_patch_from_path(path);
  switch (result.status) {
  case patches::PatchDropResult::Status::Loaded: {
    const auto before = capture_patch_snapshot();
    patch_session_.set_current_patch(result.patch, result.source_path);
    const auto after = capture_patch_snapshot();
    record_patch_change(result.history_label, before, after);
    drop.instruments.clear();
    drop.pending_instruments_path.clear();
    drop.selected_instrument = 0;
    drop.show_picker_for_multiple_instruments = false;
    drop.show_error_popup = false;
    drop.error_message.clear();
    break;
  }
  case patches::PatchDropResult::Status::MultiInstrument:
    drop.instruments = result.instruments;
    drop.pending_instruments_path = result.source_path;
    drop.selected_instrument = 0;
    drop.show_picker_for_multiple_instruments = true;
    drop.show_error_popup = false;
    drop.error_message.clear();
    break;
  case patches::PatchDropResult::Status::Error:
  default:
    drop.instruments.clear();
    drop.pending_instruments_path.clear();
    drop.selected_instrument = 0;
    drop.show_picker_for_multiple_instruments = false;
    drop.error_message = result.error_message;
    drop.show_error_popup = true;
    break;
  }
}

void AppState::apply_mml_instrument_selection(size_t index) {
  auto &drop = ui_state_.drop_state;
  if (index >= drop.instruments.size()) {
    cancel_instrument_selection();
    return;
  }

  auto selected = drop.instruments[index];
  ym2612::Patch patch_to_apply = selected;
  if (!selected.name.empty()) {
    patch_to_apply.name = selected.name;
  } else if (patch_to_apply.name.empty()) {
    patch_to_apply.name = drop.pending_instruments_path.stem().string();
  }

  const auto before = capture_patch_snapshot();
  patch_session_.set_current_patch(patch_to_apply,
                                   drop.pending_instruments_path);
  const auto after = capture_patch_snapshot();
  std::string label = "Select Instrument";
  if (!selected.name.empty()) {
    label += ": " + selected.name;
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
  return patch_session_.capture_snapshot();
}

void AppState::apply_patch_snapshot(const PatchSnapshot &snapshot) {
  patch_session_.restore_snapshot(snapshot);
}

void AppState::record_patch_change(const std::string &label,
                                   const PatchSnapshot &before,
                                   const PatchSnapshot &after) {
  history_.begin_transaction(
      label, {}, [label, before, after](AppState &state) {
        return history::make_snapshot_entry<PatchSnapshot>(
            label, std::string{}, before, after,
            [](AppState &target, const PatchSnapshot &snapshot) {
              target.patch_session_.restore_snapshot(snapshot);
            });
      });
  history_.commit_transaction(*this);
}

void AppState::apply_patch_to_device() {
  patch_session_.apply_patch_to_audio();
}
