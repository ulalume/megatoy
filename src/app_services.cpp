#include "app_services.hpp"

#include "app_state.hpp"
#include <iostream>

void AppServices::initialize_app(AppState &state) {
  path_service.ensure_directories();
  patch_session.initialize_patch_defaults();

  if (!audio_manager.initialize(SampleRate)) {
    std::cerr << "Audio manager failed to start; functionality will be limited"
              << std::endl;
  } else {
    patch_session.apply_patch_to_audio();
  }

  if (!initialize_gui("megatoy", 1000, 700)) {
    std::cerr << "GUI manager failed to start; shutting down" << std::endl;
  } else {
    gui_manager.sync_imgui_ini();
  }

  state.ui_state().prefs = preference_manager.ui_preferences();
  history.clear();
}

void AppServices::shutdown_app() {
  patch_session.release_all_notes();
  audio_manager.shutdown();
  gui_manager.shutdown();
}
