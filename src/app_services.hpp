#pragma once

#include "audio/audio_manager.hpp"
#include "gui/gui_manager.hpp"
#include "history/history_manager.hpp"
#include "patches/patch_session.hpp"
#include "preferences/preference_manager.hpp"
#include "system/path_service.hpp"
#include "ym2612/fft_analyzer.hpp"
#include <cstdint>
#include <string>

struct AppServices {
  static constexpr std::uint32_t SampleRate = 44100;

  AppServices()
      : path_service(), preference_manager(path_service), audio_manager(),
        gui_manager(preference_manager),
        patch_session(path_service, audio_manager), fft_analyzer(1024) {}

  bool initialize_gui(const std::string &title, int width, int height) {
    return gui_manager.initialize(title, width, height);
  }

  megatoy::system::PathService path_service;
  PreferenceManager preference_manager;
  AudioManager audio_manager;
  GuiManager gui_manager;
  patches::PatchSession patch_session;
  history::HistoryManager history;
  ym2612::FFTAnalyzer fft_analyzer;
};
