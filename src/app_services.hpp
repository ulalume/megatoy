#pragma once

#include "audio/audio_manager.hpp"
#include "gui/gui_manager.hpp"
#include "history/history_manager.hpp"
#include "patches/patch_session.hpp"
#include "platform/platform_services.hpp"
#include "preferences/preference_manager.hpp"
#include "system/path_service.hpp"
#include "ym2612/fft_analyzer.hpp"
#include <cstdint>
#include <string>

class AppState;

struct AppServices {
  static constexpr std::uint32_t SampleRate = 44100;

  explicit AppServices(platform::PlatformServicesProvider &platform_services)
      : platform_services_(platform_services),
        path_service(platform_services.file_system()),
        preference_manager(path_service),
        audio_manager(platform_services.create_audio_transport()),
        gui_manager(preference_manager),
        patch_session(path_service, audio_manager), fft_analyzer(1024) {}

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
  patches::PatchSession patch_session;
  history::HistoryManager history;
  ym2612::FFTAnalyzer fft_analyzer;
};
