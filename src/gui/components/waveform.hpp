#pragma once

#include "ym2612/fft_analyzer.hpp"

class AppState;

namespace ui {

void render_waveform(const char *title, AppState &app_state,
                     ym2612::FFTAnalyzer &analyzer);

}
