#include "app_services.hpp"

#include "app_state.hpp"
#include "core/status.hpp"
#include "platform/platform_config.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_patch_url.hpp"
#endif
#include <algorithm>
#include <iostream>

namespace {

std::unique_ptr<patches::PersistentParseCache> load_persistent_parse_cache() {
  auto cache = std::make_unique<patches::PersistentParseCache>();
#if defined(MEGATOY_PLATFORM_WEB)
  cache->load(megatoy::system::PathService::web_storage_root() /
              ".parse-cache.json");
#else
  cache->load(
      megatoy::system::PathService::preferences_file_path().parent_path() /
      "parse_cache.json");
#endif
  return cache;
}

} // namespace

AppServices::AppServices(platform::PlatformServicesProvider &platform_services)
    : platform_services_(platform_services),
      path_service(platform_services.file_system()),
      preference_manager(path_service),
      audio_manager(platform_services.create_audio_transport()),
      gui_manager(preference_manager),
      persistent_parse_cache(load_persistent_parse_cache()),
      patch_session(path_service, preference_manager, audio_manager,
                    persistent_parse_cache.get()),
      spectrum_analyzer(2048) {}

void AppServices::initialize_app(AppState &state) {
  path_service.ensure_directories();
  patch_session.initialize_patch_defaults();
  bool loaded_url_patch = false;
#if defined(MEGATOY_PLATFORM_WEB)
  if (auto patch = platform::web::patch_url::load_patch_from_current_url(
          patch_session.current_patch())) {
    patch_session.set_current_patch(
        *patch, {}, patches::PatchSession::RememberPatchPath::No);
    loaded_url_patch = true;
  }
#endif
  if (!loaded_url_patch) {
    patch_session.restore_patch(preference_manager.last_patch_path());
  }

  if (!audio_manager.initialize(SampleRate)) {
    megatoy::status::error(
        "Audio device unavailable -- megatoy is running without sound.");
  } else {
    patch_session.apply_patch_to_audio();
  }

  if (!initialize_gui("megatoy", 1000, 700)) {
    std::cerr << "GUI manager failed to start; shutting down" << std::endl;
  } else {
    gui_manager.sync_imgui_ini();
  }

  state.ui_state().prefs = preference_manager.ui_preferences();
  audio_manager.set_chip_type(static_cast<ym2612::ChipType>(
      std::clamp(state.ui_state().prefs.ym2612_chip_type, 0, 1)));
  audio_manager.set_note_options(
      state.ui_state().prefs.use_velocity,
      state.ui_state().prefs.velocity_sensitivity_depth,
      state.ui_state().prefs.steal_oldest_note_when_full);
  audio_manager.set_performance_options(state.ui_state().prefs.use_pitch_bend,
                                        state.ui_state().prefs.use_mod_wheel);

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
#if defined(MEGATOY_PLATFORM_DESKTOP)
  if (persistent_parse_cache && persistent_parse_cache->dirty()) {
    persistent_parse_cache->save();
  }
#endif
  audio_manager.shutdown();
  gui_manager.shutdown();
}
