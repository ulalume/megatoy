#include "waveform.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "ym2612/fft_analyzer.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <vector>

namespace ui {

void render_waveform(const char *title, WaveformContext &context,
                     ym2612::FFTAnalyzer &analyzer) {
  auto &ui_prefs = context.ui_prefs;
  if (!ui_prefs.show_waveform) {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(420.0f, 240.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(title, &ui_prefs.show_waveform)) {
    ImGui::End();
    return;
  }

  static const int sample_count = 1024;

  std::vector<float> samples;
  auto &sampler = context.sampler;
  const auto &patch = context.current_patch();

  bool has_samples = patch.channel.left_speaker || patch.channel.right_speaker;
  if (has_samples)
    sampler.latest_samples(static_cast<std::size_t>(sample_count), samples,
                           patch.channel.left_speaker);

  bool is_warning = sampler.is_volume_warning();
  if (is_warning) {
    ImGui::PushStyleColor(ImGuiCol_PlotLines,
                          styles::color_u32(styles::MegatoyCol::StatusWarning));
  }

  const ImVec2 available_region = ImGui::GetContentRegionAvail();
  const ImVec2 plot_size(
      (available_region.x - ImGui::GetStyle().ItemSpacing.x) / 2,
      available_region.y);
  ImGui::Columns(2, "waves", false);
  if (has_samples) {
    ImGui::PlotLines("Wave", samples.data(),
                     static_cast<int>(samples.size() / 2), 0, nullptr, -1.0f,
                     1.0f, plot_size);
  } else {
    ImGui::Dummy(plot_size);
  }

  ImGui::NextColumn();

  if (has_samples) {
    analyzer.compute(samples);
    const auto &mags = analyzer.magnitudes();

    // Create logarithmic frequency bins with interpolation
    const size_t num_display_bins = 256;
    std::vector<float> log_bins(num_display_bins, -60.0f);

    // Frequency-aware logarithmic mapping
    const float sample_rate = 44100.0f; // Assuming standard sample rate
    const float nyquist_freq = sample_rate / 2.0f;
    const float min_freq = 20.0f;        // 20 Hz
    const float max_freq = nyquist_freq; // Up to Nyquist

    for (size_t i = 0; i < num_display_bins; ++i) {
      // Logarithmic frequency interpolation
      float t = (float)i / (num_display_bins - 1);
      float log_freq = min_freq * std::pow(max_freq / min_freq, t);

      // Convert frequency to FFT bin with fractional indexing
      float fft_bin_f = (log_freq / nyquist_freq) * (mags.size() - 1);

      // Linear interpolation between adjacent FFT bins
      size_t bin_low = (size_t)std::floor(fft_bin_f);
      size_t bin_high = std::min(bin_low + 1, mags.size() - 1);
      float frac = fft_bin_f - bin_low;

      if (bin_low < mags.size()) {
        float mag_low = mags[bin_low];
        float mag_high = (bin_high < mags.size()) ? mags[bin_high] : mag_low;
        log_bins[i] = mag_low + frac * (mag_high - mag_low);
      }
    }
    ImGui::PlotLines("Spectrum", log_bins.data(), log_bins.size(), 0, nullptr,
                     -50.0f, 35.0f, plot_size);
  } else {
    ImGui::Dummy(plot_size);
  }

  ImGui::Columns(1);

  if (is_warning) {
    ImGui::PopStyleColor();
  }

  ImGui::End();
}

} // namespace ui
