#pragma once

#include "preferences/preference_manager.hpp"
#include "ym2612/fft_analyzer.hpp"
#include "ym2612/patch.hpp"
#include "ym2612/wave_sampler.hpp"
#include <functional>

namespace ui {

struct WaveformContext {
  PreferenceManager::UIPreferences &ui_prefs;
  ym2612::WaveSampler &sampler;
  ym2612::FFTAnalyzer &analyzer;
  std::function<const ym2612::Patch &()> current_patch;
};

void render_waveform(const char *title, WaveformContext &context);

} // namespace ui
