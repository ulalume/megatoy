#include "app_services.hpp"
#include "app_state.hpp"
#include "platform/platform_config.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_patch_url.hpp"
#endif
#include <algorithm>
#include <iostream>

void AppServices::initialize_app(AppState &state) {
  path_service.ensure_directories();
  patch_session.initialize_patch_defaults();
#if defined(MEGATOY_PLATFORM_WEB)
  if (auto patch = platform::web::patch_url::load_patch_from_current_url(
          patch_session.current_patch())) {
    patch_session.set_current_patch(*patch, {});
  }
#endif

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

  const auto &prefs = state.ui_state().prefs;
  auto &input = state.input_state();
  const auto clamp_scale = [](int value) {
    value = std::clamp(value, 0, static_cast<int>(Scale::RYUKYU));
    return static_cast<Scale>(value);
  };
  const auto clamp_key = [](int value) {
    value = std::clamp(value, 0, static_cast<int>(Key::B));
    return static_cast<Key>(value);
  };
  input.midi_keyboard_settings.scale = clamp_scale(prefs.midi_keyboard_scale);
  input.midi_keyboard_settings.key = clamp_key(prefs.midi_keyboard_key);
  input.keyboard_typing_octave =
      static_cast<uint8_t>(std::clamp(prefs.midi_keyboard_typing_octave, 0, 7));
  history.clear();
}

void AppServices::shutdown_app() {
  patch_session.release_all_notes();
  audio_manager.shutdown();
  gui_manager.shutdown();
}
