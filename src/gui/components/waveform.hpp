#pragma once

#include "audio/scope_buffer.hpp"
#include "audio/spectrum_analyzer.hpp"
#include "preferences/preference_manager.hpp"
#include "ym2612/note.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace ui {

struct WaveformContext {
  PreferenceManager::UIPreferences &ui_prefs;
  const audio::ScopeBuffer &scope;
  audio::SpectrumAnalyzer &spectrum;
  std::uint32_t sample_rate;
  /// The notes sounding, for the chord they make.
  std::function<std::vector<ym2612::Note>()> active_notes;
};

void render_waveform(const char *title, WaveformContext &context);

} // namespace ui
