#include "app_state.hpp"
#include "formats/patch_loader.hpp"
#include "patch_actions.hpp"
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

void AppState::set_connected_midi_inputs(std::vector<std::string> devices) {
  connected_midi_inputs_ = std::move(devices);
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
      patch_actions::load_dropped_patch(*this, result.patches[0], path);
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

  patch_actions::load_dropped_patch(*this, patch_to_apply,
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

void AppState::apply_patch_to_device() {
  patch_session_.apply_patch_to_audio();
}
