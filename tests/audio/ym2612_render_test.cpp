// Exercises the FM core end to end: registers in, audio out.
//
// The assertions are deliberately coarse (is there sound / is it silent / did
// the envelope restart) so they stay valid across emulation cores, while
// still catching a chip that has been wired up wrong.

#include "audio/audio_engine.hpp"
#include "ym2612/channel.hpp"
#include "ym2612/note.hpp"
#include "ym2612/patch.hpp"

#include "../test_check.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 44100;

// Instant attack, no decay, instant release: holds a steady tone while keyed
// on and stops promptly on key-off.
ym2612::Patch make_sustained_patch() {
  ym2612::Patch patch;
  patch.name = "sustained";
  patch.instrument.feedback = 7;
  patch.instrument.algorithm = 3;
  const uint8_t total_levels[4] = {48, 24, 36, 12};
  for (int op = 0; op < 4; ++op) {
    auto &settings = patch.instrument.operators[op];
    settings.attack_rate = 31;
    settings.decay_rate = 0;
    settings.sustain_level = 0;
    settings.release_rate = 15;
    settings.total_level = total_levels[op];
    settings.multiple = 1;
  }
  return patch;
}

// Instant attack followed by a fast decay to near silence, so a retrigger is
// clearly visible as the amplitude jumping back up.
ym2612::Patch make_percussive_patch() {
  ym2612::Patch patch = make_sustained_patch();
  patch.name = "percussive";
  for (auto &settings : patch.instrument.operators) {
    settings.decay_rate = 31;
    settings.sustain_level = 15;
  }
  return patch;
}

// Peak deviation from the signal's own mean.
//
// The YM2612's DAC is discontinuous around zero (the "ladder effect"), which
// ymfm reproduces; an idle chip therefore emits a small constant offset
// rather than a stream of zeroes. Measuring against the mean ignores that
// offset and reports the audible amplitude.
float render_ac_peak(AudioEngine &engine, uint32_t frames) {
  std::vector<int16_t> pcm(static_cast<size_t>(frames) * 2, 0);
  const uint32_t bytes = frames * 2 * static_cast<uint32_t>(sizeof(int16_t));
  engine.render(bytes, pcm.data());

  double sum = 0.0;
  for (int16_t sample : pcm) {
    sum += sample;
  }
  const double mean = sum / static_cast<double>(pcm.size());

  double peak = 0.0;
  for (int16_t sample : pcm) {
    peak = std::max(peak, std::abs(static_cast<double>(sample) - mean));
  }
  return static_cast<float>(peak / 32768.0);
}

void test_sample_rates() {
  ym2612::Device device;
  device.init(kSampleRate);
  CHECK(device.is_initialized());
  CHECK(device.sample_rate() == kSampleRate);
  // The YM2612 divides its clock by 144.
  CHECK(device.native_sample_rate() == ym2612::Device::kClock / 144);
  device.stop();
  CHECK(!device.is_initialized());
}

void test_key_on_produces_audio(AudioEngine &engine) {
  engine.apply_patch_to_all_channels(make_sustained_patch());

  CHECK(render_ac_peak(engine, kSampleRate / 10) < 0.001f);

  auto channel = engine.device().channel(ym2612::ChannelIndex::Fm1);
  channel.write_frequency(ym2612::Note::from_midi_note(60));
  channel.write_key_on(true, true, true, true);

  const float peak = render_ac_peak(engine, kSampleRate / 10);
  CHECK(peak > 0.01f);
  // One voice out of six must be nowhere near clipping.
  CHECK(peak < 0.5f);
}

void test_key_off_decays_to_silence(AudioEngine &engine) {
  engine.device().channel(ym2612::ChannelIndex::Fm1).write_key_off();

  // Let the release stage run out.
  render_ac_peak(engine, kSampleRate * 2);
  CHECK(render_ac_peak(engine, kSampleRate / 10) < 0.001f);
}

// A key-off immediately followed by a key-on -- which is what voice stealing
// does -- must restart the envelope instead of being swallowed as "no state
// change".
void test_retrigger_restarts_envelope(AudioEngine &engine) {
  engine.apply_patch_to_all_channels(make_percussive_patch());

  auto channel = engine.device().channel(ym2612::ChannelIndex::Fm2);
  channel.write_frequency(ym2612::Note::from_midi_note(60));
  channel.write_key_on(true, true, true, true);

  // Let the decay stage run down to the sustain level.
  render_ac_peak(engine, kSampleRate / 2);
  const float decayed = render_ac_peak(engine, kSampleRate / 20);

  // No rendering between the two writes: the chip must still observe the
  // falling edge.
  channel.write_key_off();
  channel.write_key_on(true, true, true, true);
  const float retriggered = render_ac_peak(engine, kSampleRate / 100);

  CHECK(retriggered > decayed * 4.0f);
}

} // namespace

int main() {
  test_sample_rates();

  AudioEngine engine;
  CHECK(engine.initialize(kSampleRate));
  CHECK(engine.is_running());

  test_key_on_produces_audio(engine);
  test_key_off_decays_to_silence(engine);
  test_retrigger_restarts_envelope(engine);

  std::cout << "All YM2612 render tests passed\n";
  return 0;
}
