#pragma once

#include "audio/audio_manager.hpp"
#include "audio/spectrum_analyzer.hpp"
#include "gui/gui_manager.hpp"
#include "history/history_manager.hpp"
#include "patches/patch_session.hpp"
#include "patches/persistent_parse_cache.hpp"
#include "platform/platform_services.hpp"
#include "preferences/preference_manager.hpp"
#include "system/path_service.hpp"
#include <cstdint>
#include <memory>
#include <string>

class AppState;

struct AppServices {
  static constexpr std::uint32_t SampleRate = 44100;

  explicit AppServices(platform::PlatformServicesProvider &platform_services);

  bool initialize_gui(const std::string &title, int width, int height) {
    return gui_manager.initialize(title, width, height);
  }

  void initialize_app(AppState &state);
  void shutdown_app();

  platform::PlatformServicesProvider &platform_services_;
  megatoy::system::PathService path_service;
  PreferenceManager preference_manager;
  AudioManager audio_manager;
  GuiManager gui_manager;
  std::unique_ptr<patches::PersistentParseCache> persistent_parse_cache;
  patches::PatchSession patch_session;
  history::HistoryManager history;
  audio::SpectrumAnalyzer spectrum_analyzer;
};
