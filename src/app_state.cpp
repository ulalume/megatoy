#include "app_state.hpp"
#include <iostream>

AppState::AppState(AppServices &services)
    : services_(services), input_state_(), ui_state_(),
      connected_midi_inputs_() {}

void AppState::init() {
  services_.path_service.ensure_directories();
  services_.patch_session.initialize_patch_defaults();

  if (!services_.audio_manager.initialize(AppServices::SampleRate)) {
    std::cerr << "Audio manager failed to start; functionality will be limited"
              << std::endl;
  } else {
    services_.patch_session.apply_patch_to_audio();
  }

  if (!services_.initialize_gui("megatoy", 1000, 700)) {
    std::cerr << "GUI manager failed to start; shutting down" << std::endl;
  } else {
    services_.gui_manager.sync_imgui_ini();
  }

  ui_state_.prefs = services_.preference_manager.ui_preferences();
  services_.history.clear();
}

void AppState::shutdown() {
  services_.patch_session.release_all_notes();
  services_.audio_manager.shutdown();
  services_.gui_manager.shutdown();
}

void AppState::set_connected_midi_inputs(std::vector<std::string> devices) {
  connected_midi_inputs_ = std::move(devices);
}
