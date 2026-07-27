#include "waveform.hpp"

#include "audio/scope_trigger.hpp"
#include "gui/styles/megatoy_style.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <vector>

namespace ui {

namespace {

// Samples drawn in the scope, and how far back to look for a trigger point.
constexpr std::size_t kScopeWindow = 1024;
constexpr std::size_t kTriggerSearch = 2048;
constexpr std::size_t kCaptureFrames = kScopeWindow + kTriggerSearch;

constexpr float kSpectrumTopDb = 0.0f;
constexpr float kSpectrumBottomDb = -90.0f;
constexpr float kSpectrumMinHz = 20.0f;

// How long a clipped sample keeps the warning lit, in frames of audio.
constexpr std::uint64_t kClipHoldFrames = 22050;

// Per-UI-frame release of the spectrum display.
constexpr float kSpectrumSmoothing = 0.8f;

struct Panel {
  ImDrawList *draw_list;
  ImVec2 min;
  ImVec2 max;
};

ImU32 with_alpha(ImU32 color, float alpha) {
  ImVec4 value = ImGui::ColorConvertU32ToFloat4(color);
  value.w *= alpha;
  return ImGui::ColorConvertFloat4ToU32(value);
}

Panel begin_panel(const ImVec2 &size, const char *id) {
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(id, size);

  Panel panel{ImGui::GetWindowDrawList(), origin,
              ImVec2(origin.x + size.x, origin.y + size.y)};
  panel.draw_list->AddRectFilled(panel.min, panel.max,
                                 ImGui::GetColorU32(ImGuiCol_FrameBg));
  panel.draw_list->PushClipRect(panel.min, panel.max, true);
  return panel;
}

void end_panel(const Panel &panel) {
  panel.draw_list->PopClipRect();
  panel.draw_list->AddRect(panel.min, panel.max,
                           ImGui::GetColorU32(ImGuiCol_Border));
}

void draw_channel(const Panel &panel, const std::vector<float> &samples,
                  std::size_t offset, std::size_t count, float mean,
                  ImU32 color) {
  if (count < 2) {
    return;
  }

  const float width = panel.max.x - panel.min.x;
  const float height = panel.max.y - panel.min.y;
  const float center_y = panel.min.y + height * 0.5f;
  const float half_height = height * 0.5f;

  // One vertex per horizontal pixel at most: a 1024-sample trace across a
  // 200px panel would otherwise cost five times the geometry for no visible
  // benefit.
  const std::size_t steps =
      std::min<std::size_t>(count, static_cast<std::size_t>(
                                       std::max(2.0f, std::floor(width))));

  panel.draw_list->PathClear();
  for (std::size_t step = 0; step < steps; ++step) {
    const std::size_t index =
        offset + (step * (count - 1)) / (steps - 1);
    const float value = std::clamp(samples[index] - mean, -1.0f, 1.0f);
    const float x =
        panel.min.x + width * static_cast<float>(step) /
                          static_cast<float>(steps - 1);
    panel.draw_list->PathLineTo(ImVec2(x, center_y - value * half_height));
  }
  panel.draw_list->PathStroke(color, ImDrawFlags_None, 1.5f);
}

float mean_of(const std::vector<float> &samples) {
  if (samples.empty()) {
    return 0.0f;
  }
  double sum = 0.0;
  for (float sample : samples) {
    sum += sample;
  }
  return static_cast<float>(sum / static_cast<double>(samples.size()));
}

void draw_scope(const Panel &panel, const std::vector<float> &left,
                const std::vector<float> &right,
                const std::vector<float> &mono, bool clipped) {
  const float height = panel.max.y - panel.min.y;
  const ImU32 grid = with_alpha(ImGui::GetColorU32(ImGuiCol_Border), 0.6f);

  // Zero line plus +/-0.5 markers.
  for (float level : {-0.5f, 0.0f, 0.5f}) {
    const float y = panel.min.y + height * (0.5f - level * 0.5f);
    panel.draw_list->AddLine(ImVec2(panel.min.x, y), ImVec2(panel.max.x, y),
                             level == 0.0f
                                 ? grid
                                 : with_alpha(grid, 0.5f));
  }

  if (left.size() < 2) {
    return;
  }

  // Trigger on the summed signal so both channels stay in step with each
  // other; find_trigger removes the mean itself, ignoring the DAC's DC
  // offset.
  const std::size_t window = std::min(kScopeWindow, left.size());
  const std::size_t start =
      audio::find_trigger(mono.data(), mono.size(), window);

  const ImU32 left_color =
      clipped ? styles::color_u32(styles::MegatoyCol::StatusWarning)
              : ImGui::GetColorU32(ImGuiCol_PlotLines);
  const ImU32 right_color =
      clipped ? styles::color_u32(styles::MegatoyCol::StatusWarning)
              : styles::color_u32(styles::MegatoyCol::TextHighlight);

  draw_channel(panel, right, start, window, mean_of(right),
               with_alpha(right_color, 0.75f));
  draw_channel(panel, left, start, window, mean_of(left), left_color);
}

void draw_spectrum(const Panel &panel, const audio::SpectrumAnalyzer &spectrum,
                   std::uint32_t sample_rate) {
  const float width = panel.max.x - panel.min.x;
  const float height = panel.max.y - panel.min.y;
  if (width < 4.0f || height < 4.0f || sample_rate == 0) {
    return;
  }

  const auto &magnitudes = spectrum.magnitudes_db();
  if (magnitudes.size() < 2) {
    return;
  }

  const float nyquist = static_cast<float>(sample_rate) * 0.5f;
  const float log_min = std::log10(kSpectrumMinHz);
  const float log_max = std::log10(nyquist);
  const float log_span = log_max - log_min;

  const ImU32 grid = with_alpha(ImGui::GetColorU32(ImGuiCol_Border), 0.5f);
  const ImU32 label = with_alpha(
      styles::color_u32(styles::MegatoyCol::TextMuted), 0.9f);

  auto x_for_hz = [&](float hz) {
    const float t = (std::log10(std::max(hz, kSpectrumMinHz)) - log_min) /
                    log_span;
    return panel.min.x + width * std::clamp(t, 0.0f, 1.0f);
  };
  auto y_for_db = [&](float db) {
    const float t = (kSpectrumTopDb - db) /
                    (kSpectrumTopDb - kSpectrumBottomDb);
    return panel.min.y + height * std::clamp(t, 0.0f, 1.0f);
  };

  for (float db = kSpectrumTopDb - 20.0f; db > kSpectrumBottomDb; db -= 20.0f) {
    const float y = y_for_db(db);
    panel.draw_list->AddLine(ImVec2(panel.min.x, y), ImVec2(panel.max.x, y),
                             grid);
    char text[16];
    std::snprintf(text, sizeof(text), "%.0f", db);
    panel.draw_list->AddText(ImVec2(panel.min.x + 3.0f, y + 1.0f), label,
                             text);
  }

  for (float hz : {100.0f, 1000.0f, 10000.0f}) {
    if (hz >= nyquist) {
      continue;
    }
    const float x = x_for_hz(hz);
    panel.draw_list->AddLine(ImVec2(x, panel.min.y), ImVec2(x, panel.max.y),
                             grid);
    const char *text = hz < 1000.0f ? "100" : (hz < 10000.0f ? "1k" : "10k");
    panel.draw_list->AddText(
        ImVec2(x + 2.0f, panel.max.y - ImGui::GetTextLineHeight() - 1.0f),
        label, text);
  }

  // One column per pixel, each taking the loudest bin that maps to it, so
  // narrow peaks survive the log-frequency squeeze at the top end.
  const int columns = static_cast<int>(width);
  std::vector<float> peaks(static_cast<std::size_t>(columns),
                           audio::SpectrumAnalyzer::kFloorDb);

  for (std::size_t bin = 1; bin < magnitudes.size(); ++bin) {
    const float hz = spectrum.bin_frequency(bin, sample_rate);
    if (hz < kSpectrumMinHz) {
      continue;
    }
    const float t = (std::log10(hz) - log_min) / log_span;
    int column = static_cast<int>(t * static_cast<float>(columns));
    column = std::clamp(column, 0, columns - 1);
    peaks[static_cast<std::size_t>(column)] =
        std::max(peaks[static_cast<std::size_t>(column)], magnitudes[bin]);
  }

  // Below ~1 kHz the bins are wider than a pixel, leaving gaps; carry the
  // previous column's value across them.
  float carried = audio::SpectrumAnalyzer::kFloorDb;
  const ImU32 fill = with_alpha(ImGui::GetColorU32(ImGuiCol_PlotHistogram),
                                0.85f);
  for (int column = 0; column < columns; ++column) {
    float db = peaks[static_cast<std::size_t>(column)];
    if (db <= audio::SpectrumAnalyzer::kFloorDb) {
      db = carried;
    } else {
      carried = db;
    }
    if (db <= kSpectrumBottomDb) {
      continue;
    }
    const float x = panel.min.x + static_cast<float>(column);
    panel.draw_list->AddLine(ImVec2(x, y_for_db(db)),
                             ImVec2(x, panel.max.y), fill);
  }
}

} // namespace

void render_waveform(const char *title, WaveformContext &context) {
  auto &ui_prefs = context.ui_prefs;
  if (!ui_prefs.show_waveform) {
    return;
  }

  ImGuiWindowClass window_class;
  window_class.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_AutoHideTabBar;
  ImGui::SetNextWindowClass(&window_class);

  ImGui::SetNextWindowSize(ImVec2(420.0f, 240.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(title, &ui_prefs.show_waveform)) {
    ImGui::End();
    return;
  }

  static std::vector<float> left;
  static std::vector<float> right;
  static std::vector<float> mono;

  context.scope.snapshot(kCaptureFrames, left, right);

  mono.resize(left.size());
  for (std::size_t i = 0; i < left.size(); ++i) {
    mono[i] = (left[i] + right[i]) * 0.5f;
  }
  context.spectrum.analyze(mono.data(), mono.size(), kSpectrumSmoothing);

  const bool clipped = context.scope.clipped_within(kClipHoldFrames);

  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const ImVec2 panel_size(std::max(32.0f, (available.x - spacing) * 0.5f),
                          std::max(32.0f, available.y));

  const Panel scope_panel = begin_panel(panel_size, "##scope");
  draw_scope(scope_panel, left, right, mono, clipped);
  end_panel(scope_panel);

  ImGui::SameLine();

  const Panel spectrum_panel = begin_panel(panel_size, "##spectrum");
  draw_spectrum(spectrum_panel, context.spectrum, context.sample_rate);
  end_panel(spectrum_panel);

  ImGui::End();
}

} // namespace ui
