#include "waveform.hpp"

#include "../app_state.hpp"
// #include "../channel_allocator.hpp"
// #include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>
#include <vector>

namespace ui {

void render_waveform(AppState &app_state) {
  auto &ui_state = app_state.ui_state();
  if (!ui_state.prefs.show_waveform) {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(420.0f, 240.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Waveform", &ui_state.prefs.show_waveform)) {
    ImGui::End();
    return;
  }

  static int sample_count = 256;
  // sample_count = std::clamp(
  //     sample_count, 32,
  //     static_cast<int>(ym2612::WaveSampler::buffer_size()));

  // ImGui::SliderInt("Samples", &sample_count, 64,
  //                  static_cast<int>(ym2612::WaveSampler::buffer_size()));

  std::vector<float> left;
  std::vector<float> right;
  auto &sampler = app_state.wave_sampler();
  sampler.latest_samples(static_cast<std::size_t>(sample_count), left, right);

  const ImVec2 available_region = ImGui::GetContentRegionAvail();
  const ImVec2 plot_size(
      (available_region.x - ImGui::GetStyle().ItemSpacing.x) / 2,
      available_region.y);
  ImGui::Columns(2, "waves", false);
  if (!left.empty()) {
    ImGui::PlotLines("Left", left.data(), static_cast<int>(left.size()), 0,
                     nullptr, -1.0f, 1.0f, plot_size);
  } else {
    ImGui::Dummy(plot_size);
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "No samples yet");
  }
  ImGui::NextColumn();
  if (!right.empty()) {
    ImGui::PlotLines("Right", right.data(), static_cast<int>(right.size()), 0,
                     nullptr, -1.0f, 1.0f, plot_size);
  } else {
    ImGui::Dummy(plot_size);
  }
  ImGui::Columns(1);

  // ImGui::Separator();

  // const auto &channel_usage = app_state.channel_allocator().channel_usage();
  // ImGui::TextUnformatted("Channel Status");

  // const ImVec4 active_color(0.2f, 0.8f, 0.3f, 1.0f);
  // const ImVec4 inactive_color(0.6f, 0.6f, 0.6f, 1.0f);

  // ImGui::Columns(6, "channel_status", false);
  // for (std::size_t i = 0; i < channel_usage.size(); ++i) {
  //   const bool active = channel_usage[i];
  //   ImGui::TextColored(active ? active_color : inactive_color, "CH%zu\n%s",
  //                      i + 1, active ? "ON" : "OFF");
  //   ImGui::NextColumn();
  // }
  // ImGui::Columns(1);

  ImGui::End();
}

} // namespace ui
