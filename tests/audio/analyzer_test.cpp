// Checks that the visualization actually measures what it claims to: dBFS on
// the spectrum's vertical axis, correct bin frequencies, and a scope trigger
// that holds a waveform still.

#include "audio/scope_buffer.hpp"
#include "audio/scope_trigger.hpp"
#include "audio/spectrum_analyzer.hpp"

#include "../test_check.hpp"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 44100;
constexpr double kPi = 3.14159265358979323846;

std::vector<float> make_sine(float hz, float amplitude, std::size_t count,
                            float phase = 0.0f, float dc = 0.0f) {
  std::vector<float> samples(count);
  for (std::size_t i = 0; i < count; ++i) {
    const double t = static_cast<double>(i) / kSampleRate;
    samples[i] = dc + amplitude * static_cast<float>(std::sin(
                                      2.0 * kPi * hz * t + phase));
  }
  return samples;
}

std::size_t peak_bin(const audio::SpectrumAnalyzer &spectrum) {
  const auto &magnitudes = spectrum.magnitudes_db();
  std::size_t best = 0;
  for (std::size_t bin = 1; bin < magnitudes.size(); ++bin) {
    if (magnitudes[bin] > magnitudes[best]) {
      best = bin;
    }
  }
  return best;
}

// A full-scale sine must read 0 dBFS, and half amplitude must read -6 dB.
// Without a correct window normalization these numbers drift arbitrarily,
// which is what makes a spectrum display meaningless.
void test_spectrum_levels_are_dbfs() {
  constexpr std::size_t kFftSize = 2048;
  audio::SpectrumAnalyzer spectrum(kFftSize);

  // Sit exactly on bin 46 (~990 Hz). A tone between bins loses up to 1.4 dB
  // to Hann scalloping, which would hide a genuine calibration error.
  const float hz = 46.0f * kSampleRate / kFftSize;

  const auto full = make_sine(hz, 1.0f, 4096);
  spectrum.analyze(full.data(), full.size(), 0.0f);

  const std::size_t bin = peak_bin(spectrum);
  CHECK(bin == 46);
  CHECK(std::fabs(spectrum.bin_frequency(bin, kSampleRate) - hz) < 0.01f);
  CHECK(std::fabs(spectrum.magnitudes_db()[bin]) < 0.2f);

  spectrum.reset();
  const auto half = make_sine(hz, 0.5f, 4096);
  spectrum.analyze(half.data(), half.size(), 0.0f);
  const float half_db = spectrum.magnitudes_db()[peak_bin(spectrum)];
  CHECK(std::fabs(half_db - (-6.02f)) < 0.2f);
}

// An arbitrary frequency still has to land in the right place and stay within
// Hann's known scalloping loss of the true level.
void test_spectrum_handles_off_bin_frequencies() {
  audio::SpectrumAnalyzer spectrum(2048);
  const float hz = 1000.0f;
  const auto tone = make_sine(hz, 1.0f, 4096);
  spectrum.analyze(tone.data(), tone.size(), 0.0f);

  const std::size_t bin = peak_bin(spectrum);
  const float bin_width = static_cast<float>(kSampleRate) / 2048.0f;
  CHECK(std::fabs(spectrum.bin_frequency(bin, kSampleRate) - hz) < bin_width);

  const float db = spectrum.magnitudes_db()[bin];
  CHECK(db <= 0.2f);
  CHECK(db > -1.5f);
}

void test_silence_reads_floor() {
  audio::SpectrumAnalyzer spectrum(2048);
  const std::vector<float> silence(4096, 0.0f);
  spectrum.analyze(silence.data(), silence.size(), 0.0f);
  for (float db : spectrum.magnitudes_db()) {
    CHECK(db <= audio::SpectrumAnalyzer::kFloorDb + 0.01f);
  }
}

// The YM2612's DAC emits a constant offset even when idle. If it were not
// removed it would swamp the low bins and make the display look busy during
// silence.
void test_dc_offset_is_removed() {
  audio::SpectrumAnalyzer spectrum(2048);
  const std::vector<float> dc(4096, 0.25f);
  spectrum.analyze(dc.data(), dc.size(), 0.0f);
  for (float db : spectrum.magnitudes_db()) {
    CHECK(db < -60.0f);
  }
}

// The whole point of triggering: the same waveform captured at different
// offsets must produce the same picture.
void test_trigger_stabilizes_waveform() {
  constexpr std::size_t kWindow = 512;
  const float hz = 440.0f;

  const auto reference = make_sine(hz, 0.8f, 3072, 0.0f, 0.3f);
  const std::size_t reference_start =
      audio::find_trigger(reference.data(), reference.size(), kWindow);

  // Same signal, shifted by a fraction of a period and with a different DC
  // offset -- as if the audio thread had run a little further ahead.
  const float shift = 2.0f * static_cast<float>(kPi) * 0.37f;
  const auto shifted = make_sine(hz, 0.8f, 3072, shift, -0.2f);
  const std::size_t shifted_start =
      audio::find_trigger(shifted.data(), shifted.size(), kWindow);

  // Compare the two triggered windows with their DC removed.
  double error = 0.0;
  for (std::size_t i = 0; i < kWindow; ++i) {
    const float a = reference[reference_start + i] - 0.3f;
    const float b = shifted[shifted_start + i] + 0.2f;
    error = std::max(error, std::fabs(static_cast<double>(a - b)));
  }
  // One sample of alignment slack at 440 Hz is about 0.05 in amplitude.
  CHECK(error < 0.1);
}

void test_trigger_handles_short_and_silent_input() {
  const std::vector<float> silence(2048, 0.0f);
  CHECK(audio::find_trigger(silence.data(), silence.size(), 512) ==
        silence.size() - 512);

  const std::vector<float> tiny(100, 0.0f);
  CHECK(audio::find_trigger(tiny.data(), tiny.size(), 512) == 0);
  CHECK(audio::find_trigger(nullptr, 0, 512) == 0);
}

void test_scope_buffer_keeps_newest_frames() {
  audio::ScopeBuffer buffer;
  const std::size_t frames = audio::ScopeBuffer::kCapacity + 777;

  std::vector<float> block(frames * 2);
  for (std::size_t i = 0; i < frames; ++i) {
    block[i * 2 + 0] = static_cast<float>(i % 1000) / 1000.0f;
    block[i * 2 + 1] = -block[i * 2 + 0];
  }
  buffer.write(block.data(), frames);
  CHECK(buffer.frames_written() == frames);

  std::vector<float> left;
  std::vector<float> right;
  buffer.snapshot(256, left, right);
  CHECK(left.size() == 256);

  // snapshot() returns the newest frames, oldest first.
  for (std::size_t i = 0; i < left.size(); ++i) {
    const std::size_t source = frames - 256 + i;
    CHECK(left[i] == block[source * 2 + 0]);
    CHECK(right[i] == block[source * 2 + 1]);
  }

  // Asking for more than the buffer holds yields what it has, not garbage.
  buffer.snapshot(audio::ScopeBuffer::kCapacity * 2, left, right);
  CHECK(left.size() == audio::ScopeBuffer::kCapacity);
}

void test_scope_buffer_reports_clipping() {
  audio::ScopeBuffer buffer;
  const std::vector<float> quiet(512 * 2, 0.5f);
  buffer.write(quiet.data(), 512);
  CHECK(!buffer.clipped_within(1024));

  const std::vector<float> loud(8 * 2, 1.0f);
  buffer.write(loud.data(), 8);
  CHECK(buffer.clipped_within(1024));

  // The warning expires once the clipped samples are far enough in the past.
  buffer.write(quiet.data(), 512);
  buffer.write(quiet.data(), 512);
  buffer.write(quiet.data(), 512);
  CHECK(!buffer.clipped_within(1024));
}

} // namespace

int main() {
  test_spectrum_levels_are_dbfs();
  test_spectrum_handles_off_bin_frequencies();
  test_silence_reads_floor();
  test_dc_offset_is_removed();
  test_trigger_stabilizes_waveform();
  test_trigger_handles_short_and_silent_input();
  test_scope_buffer_keeps_newest_frames();
  test_scope_buffer_reports_clipping();

  std::cout << "All analyzer tests passed\n";
  return 0;
}
