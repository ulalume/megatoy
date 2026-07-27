#pragma once

#include "audio/scope_buffer.hpp"
#include "audio/spectrum_analyzer.hpp"
#include "preferences/preference_manager.hpp"

#include <cstdint>

namespace ui {

struct WaveformContext {
  PreferenceManager::UIPreferences &ui_prefs;
  audio::ScopeBuffer &scope;
  audio::SpectrumAnalyzer &spectrum;
  std::uint32_t sample_rate;
};

void render_waveform(const char *title, WaveformContext &context);

} // namespace ui
