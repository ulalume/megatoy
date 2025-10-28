#include "app_state.hpp"
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

void AppState::set_connected_midi_inputs(std::vector<std::string> devices) {
  connected_midi_inputs_ = std::move(devices);
}
