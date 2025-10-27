#include "app_state.hpp"

#include "formats/patch_loader.hpp"
#include "history/snapshot_entry.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>

AppState::AppState()
    : path_service_(), preference_manager_(path_service_), audio_manager_(),
      gui_manager_(preference_manager_),
      patch_session_(path_service_, audio_manager_), input_state_(),
      ui_state_(), history_(), connected_midi_inputs_() {}

void AppState::init() {
  path_service_.ensure_directories();
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
const std::vector<ym2612::Note> AppState::active_notes() const {
  return patch_session_.active_notes();
}

void AppState::set_connected_midi_inputs(std::vector<std::string> devices) {
  connected_midi_inputs_ = std::move(devices);
}

bool AppState::load_patch(const patches::PatchEntry &patch_info) {
  const auto before = capture_patch_snapshot();

  ym2612::Patch loaded_patch;
  if (!patch_session_.repository().load_patch(patch_info, loaded_patch)) {
    std::cerr << "Failed to load preset patch: " << patch_info.name
              << std::endl;
    return false;
  }

  patch_session_.current_patch() = loaded_patch;
  patch_session_.set_current_patch_path(patch_info.relative_path);
  patch_session_.apply_patch_to_audio();

  const auto after = capture_patch_snapshot();
  record_patch_change("Load: " + patch_info.name, before, after);
  patch_session_.mark_as_clean(); // Mark as clean after successful load
  std::cout << "Loaded preset patch: " << patch_info.name << std::endl;
  return true;
}

void AppState::safe_load_patch(const patches::PatchEntry &preset_info) {
  if (patch_session_.is_modified()) {
    // Show confirmation dialog
    ui_state_.confirmation_state =
        UIState::ConfirmationState::load(preset_info);
  } else {
    // Load directly if no modifications
    load_patch(preset_info);
  }
}

void AppState::load_dropped_patch_with_history(
    const ym2612::Patch &patch, const std::filesystem::path &source_path) {
  const auto before = capture_patch_snapshot();
  patch_session_.set_current_patch(patch, source_path);
  const auto after = capture_patch_snapshot();
  record_patch_change("Load: " + source_path.filename().string(), before,
                      after);
}

void AppState::sync_patch_directories() {
  path_service_.ensure_directories();
  patch_session_.refresh_directories();
}

void AppState::sync_imgui_ini_file() { gui_manager_.sync_imgui_ini(); }

void AppState::handle_patch_file_drop(const std::filesystem::path &path) {
  auto &drop = ui_state_.drop_state;
  drop.error_message.clear();

  const auto result = formats::load_patch_from_file(path);

  switch (result.status) {
  case formats::PatchLoadStatus::Success: {
    if (patch_session_.is_modified()) {
      // Show confirmation dialog for file drop
      ui_state_.confirmation_state = UIState::ConfirmationState::drop();
      // Store the dropped patch for later use
      drop.pending_dropped_patch = result.patches[0];
      drop.pending_dropped_path = path;
    } else {
      load_dropped_patch_with_history(result.patches[0], path);
    }
    drop.instruments.clear();
    drop.pending_instruments_path.clear();
    drop.selected_instrument = 0;
    drop.show_picker_for_multiple_instruments = false;
    drop.show_error_popup = false;
    drop.error_message.clear();
    break;
  }
  case formats::PatchLoadStatus::MultiInstrument:
    drop.instruments = result.patches;
    drop.pending_instruments_path = path;
    drop.selected_instrument = 0;
    drop.show_picker_for_multiple_instruments = true;
    drop.show_error_popup = false;
    drop.error_message.clear();
    break;
  case formats::PatchLoadStatus::Failure:
  default:
    drop.instruments.clear();
    drop.pending_instruments_path.clear();
    drop.selected_instrument = 0;
    drop.show_picker_for_multiple_instruments = false;
    drop.error_message = result.message;
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

  load_dropped_patch_with_history(patch_to_apply,
                                  drop.pending_instruments_path);

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

void AppState::record_patch_change(const std::string &label,
                                   const PatchSnapshot &before,
                                   const PatchSnapshot &after) {
  history_.begin_transaction(
      label, {}, [label, before, after](AppState &state) {
        return history::make_snapshot_entry<PatchSnapshot>(
            label, std::string{}, before, after,
            [](AppState &target, const PatchSnapshot &snapshot) {
              target.patch_session_.restore_snapshot(snapshot);

              std::cout << "Patch restored" << std::endl;
            });
      });
  history_.commit_transaction(*this);
}

void AppState::apply_patch_to_device() {
  patch_session_.apply_patch_to_audio();
}
