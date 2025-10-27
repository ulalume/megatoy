#pragma once

#include "../app_state.hpp"
#include "../ym2612/fft_analyzer.hpp"

namespace ui {

void render_all(AppState &app_state, ym2612::FFTAnalyzer &analyzer);

} // namespace ui
