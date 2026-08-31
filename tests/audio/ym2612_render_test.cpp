// Exercises the FM core end to end: registers in, audio out.
//
// The assertions are deliberately coarse (is there sound / is it silent / did
// the envelope restart) so they stay valid across emulation cores, while
// still catching a chip that has been wired up wrong.

#include "audio/audio_engine.hpp"
#include "audio/lowpass_filter.hpp"
#include "ym2612/channel.hpp"
#include "ym2612/note.hpp"
#include "ym2612/nuked_chip.hpp"
#include "ym2612/patch.hpp"
#include "ym2612/ymfm_chip.hpp"

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

ym2612::Patch make_all_carrier_patch(uint8_t total_level) {
  ym2612::Patch patch;
  patch.name = "all carriers";
  patch.instrument.algorithm = 7;
  for (auto &settings : patch.instrument.operators) {
    settings.attack_rate = 31;
    settings.decay_rate = 0;
    settings.sustain_rate = 0;
    settings.sustain_level = 0;
    settings.release_rate = 15;
    settings.total_level = total_level;
    settings.multiple = 1;
  }
  return patch;
}

void submit_patch(AudioEngine &engine, const ym2612::Patch &patch) {
  CHECK(engine.submit(audio::AudioCommand::apply_patch(
      patch.global, patch.channel, patch.instrument)));
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

void test_raw_idle_dc_by_chip_type() {
  ym2612::YmfmChip chip(ym2612::Device::kClock);
  constexpr uint32_t kFrames = 16;
  int32_t left[kFrames] = {};
  int32_t right[kFrames] = {};

  CHECK(chip.chip_type() == ym2612::ChipType::Ym2612);
  chip.render(left, right, kFrames);
  CHECK(std::abs(left[kFrames - 1] - 504) <= 1);
  CHECK(std::abs(right[kFrames - 1] - 504) <= 1);
  CHECK(left[kFrames - 1] != 0);

  chip.set_chip_type(ym2612::ChipType::Ym3438);
  chip.render(left, right, kFrames);
  CHECK(left[kFrames - 1] == 0);
  CHECK(right[kFrames - 1] == 0);
}

// The Nuked core stops being clocked once it has nothing left to play, so an
// idle stretch is made of samples the pause itself produces. They have to be
// the offset the chip emits, held for the whole stretch: a run of zeroes
// would step the signal at both ends of the pause.
void test_nuked_raw_idle_dc_by_chip_type() {
  ym2612::NukedChip chip(ym2612::Device::kClock);
  constexpr uint32_t kFrames = 4096;
  std::vector<int32_t> left(kFrames, 0);
  std::vector<int32_t> right(kFrames, 0);

  const auto rendered_range = [&](int32_t &lowest, int32_t &highest) {
    chip.render(left.data(), right.data(), kFrames);
    lowest = left[0];
    highest = left[0];
    for (uint32_t i = 0; i < kFrames; ++i) {
      lowest = std::min({lowest, left[i], right[i]});
      highest = std::max({highest, left[i], right[i]});
    }
  };

  int32_t lowest = 0;
  int32_t highest = 0;

  CHECK(chip.chip_type() == ym2612::ChipType::Ym2612);
  rendered_range(lowest, highest);
  CHECK(lowest == 512);
  CHECK(highest == 512);

  chip.set_chip_type(ym2612::ChipType::Ym3438);
  rendered_range(lowest, highest);
  CHECK(lowest == 0);
  CHECK(highest == 0);
}

// A note released into a long silence and then retriggered. The core stops
// somewhere in the middle of that silence and starts again on the write, and
// the samples across both boundaries have to sit at the same level.
void test_nuked_idle_stretch_holds_its_level() {
  constexpr float kIdleLevel = 512.0f / 32768.0f;

  ym2612::Device device;
  device.init(kSampleRate);
  device.set_core_type(ym2612::CoreType::Nuked);
  CHECK(device.core_type() == ym2612::CoreType::Nuked);

  const auto patch = make_all_carrier_patch(20);
  auto channel = device.channel(ym2612::ChannelIndex::Fm1);
  channel.write_settings(patch.channel);
  channel.write_instrument(patch.instrument);
  channel.write_frequency(ym2612::Note::from_midi_note(60));
  channel.write_key_on(true, true, true, true);

  std::vector<float> sounding(static_cast<size_t>(kSampleRate) / 10 * 2, 0.0f);
  device.render(kSampleRate / 10, sounding.data());
  channel.write_key_off();

  // Long enough for the release to run out with the pause still to come.
  const uint32_t idle_frames = kSampleRate / 2;
  std::vector<float> idle(static_cast<size_t>(idle_frames) * 2, 0.0f);
  device.render(idle_frames, idle.data());

  // The second half of the stretch is entirely past the release.
  float largest_step = 0.0f;
  for (size_t i = idle.size() / 2; i < idle.size(); ++i) {
    largest_step = std::max(largest_step, std::abs(idle[i] - kIdleLevel));
  }
  CHECK(largest_step == 0.0f);

  channel.write_key_on(true, true, true, true);
  std::vector<float> again(static_cast<size_t>(kSampleRate) / 20 * 2, 0.0f);
  device.render(kSampleRate / 20, again.data());
  float peak = 0.0f;
  for (float sample : again) {
    peak = std::max(peak, std::abs(sample - kIdleLevel));
  }
  CHECK(peak > 0.01f);
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

void test_ctrmml_lowpass_response() {
  audio::LowPassFilter filter;
  filter.init(kSampleRate);

  std::int32_t left = 65536;
  std::int32_t right = 65536;
  filter.apply(left, right);
  const std::int32_t first = left;
  CHECK(first == right);
  CHECK(first == 21344);

  left = right = 65536;
  filter.apply(left, right);
  CHECK(left > first);
  CHECK(left < 65536);
  CHECK(left == right);
}

void test_key_on_produces_audio(AudioEngine &engine) {
  engine.apply_patch_to_all_channels(make_sustained_patch());

  // Let the DC blocker absorb the chip's startup offset before asserting
  // silence.
  render_ac_peak(engine, kSampleRate / 2);
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

// The chip idles at a constant DAC offset; the engine must remove it so an
// idle app emits true digital silence. Shipping the offset to the DAC made
// amplifiers gate in and out of standby, popping every few seconds.
void test_idle_settles_to_digital_silence() {
  AudioEngine engine;
  CHECK(engine.initialize(kSampleRate));

  // One second is over twenty time constants of the blocker.
  std::vector<int16_t> pcm(static_cast<size_t>(kSampleRate) * 2, 0);
  engine.render(kSampleRate * 2 * static_cast<uint32_t>(sizeof(int16_t)),
                pcm.data());

  engine.render(kSampleRate / 2 * static_cast<uint32_t>(sizeof(int16_t)),
                pcm.data());
  int peak = 0;
  for (size_t i = 0; i < static_cast<size_t>(kSampleRate) / 2; ++i) {
    peak = std::max(peak, std::abs(static_cast<int>(pcm[i])));
  }
  CHECK(peak <= 1);
}

void test_apply_patch_reaches_sustaining_note() {
  AudioEngine engine;
  CHECK(engine.initialize(kSampleRate));

  const auto note = ym2612::Note::from_midi_note(60);
  const auto loud_patch = make_all_carrier_patch(20);
  submit_patch(engine, loud_patch);
  CHECK(engine.submit(audio::AudioCommand::note_on(note, 127)));
  const float loud = render_ac_peak(engine, kSampleRate / 10);

  const auto quiet_patch = make_all_carrier_patch(110);
  submit_patch(engine, quiet_patch);
  render_ac_peak(engine, kSampleRate / 20);
  const float quiet = render_ac_peak(engine, kSampleRate / 10);

  CHECK(loud > 0.01f);
  CHECK(quiet < loud * 0.25f);
}

void test_apply_patch_updates_instrument_for_future_notes() {
  AudioEngine engine;
  CHECK(engine.initialize(kSampleRate));

  const auto first_note = ym2612::Note::from_midi_note(60);
  submit_patch(engine, make_all_carrier_patch(20));
  CHECK(engine.submit(audio::AudioCommand::note_on(first_note, 127)));
  const float first_peak = render_ac_peak(engine, kSampleRate / 10);
  CHECK(engine.submit(audio::AudioCommand::note_off(first_note)));
  render_ac_peak(engine, kSampleRate / 10);

  const auto second_note = ym2612::Note::from_midi_note(64);
  submit_patch(engine, make_all_carrier_patch(100));
  CHECK(engine.submit(audio::AudioCommand::note_on(second_note, 127)));
  const float second_peak = render_ac_peak(engine, kSampleRate / 10);

  CHECK(first_peak > 0.01f);
  CHECK(second_peak < first_peak * 0.35f);
}

void test_apply_patch_preserves_sustaining_note_velocity() {
  AudioEngine engine;
  CHECK(engine.initialize(kSampleRate));
  engine.set_note_options(true, 100, true);

  auto patch = make_all_carrier_patch(20);
  submit_patch(engine, patch);
  CHECK(engine.submit(
      audio::AudioCommand::note_on(ym2612::Note::from_midi_note(60), 40)));
  const float before = render_ac_peak(engine, kSampleRate / 10);

  patch.global.lfo_enable = true;
  submit_patch(engine, patch);
  const float after = render_ac_peak(engine, kSampleRate / 10);

  CHECK(before > 0.001f);
  CHECK(after < before * 1.5f);
  CHECK(after > before * 0.5f);
}

void test_switching_chip_type_releases_notes_and_keeps_audio_working() {
  AudioEngine engine;
  CHECK(engine.initialize(kSampleRate));

  const auto patch = make_all_carrier_patch(20);
  submit_patch(engine, patch);
  const auto first_note = ym2612::Note::from_midi_note(60);
  CHECK(engine.submit(audio::AudioCommand::note_on(first_note, 127)));
  CHECK(render_ac_peak(engine, kSampleRate / 10) > 0.01f);
  CHECK(engine.notes().published_contains(first_note));

  CHECK(engine.submit(
      audio::AudioCommand::set_chip_type(ym2612::ChipType::Ym3438)));
  render_ac_peak(engine, 64);
  CHECK(engine.device().chip_type() == ym2612::ChipType::Ym3438);
  CHECK(engine.notes().published_notes().empty());

  const auto second_note = ym2612::Note::from_midi_note(64);
  CHECK(engine.submit(audio::AudioCommand::note_on(second_note, 127)));
  CHECK(render_ac_peak(engine, kSampleRate / 10) > 0.01f);

  CHECK(engine.submit(audio::AudioCommand::note_off(second_note)));
  render_ac_peak(engine, kSampleRate * 2);

  std::vector<float> idle(static_cast<size_t>(kSampleRate / 10) * 2, 1.0f);
  engine.device().render(kSampleRate / 10, idle.data());
  CHECK(std::abs(idle[idle.size() - 2]) < 0.000001f);
  CHECK(std::abs(idle[idle.size() - 1]) < 0.000001f);
}

// The core is swapped on the audio thread, and the chip it hands over to
// starts blank: the engine has to re-establish the registers before the next
// note, exactly as it does for a chip-type change.
void test_switching_core_releases_notes_and_keeps_audio_working() {
  AudioEngine engine;
  CHECK(engine.initialize(kSampleRate));
  CHECK(engine.device().core_type() == ym2612::CoreType::Ymfm);

  const auto patch = make_all_carrier_patch(20);
  submit_patch(engine, patch);
  const auto first_note = ym2612::Note::from_midi_note(60);
  CHECK(engine.submit(audio::AudioCommand::note_on(first_note, 127)));
  const float before = render_ac_peak(engine, kSampleRate / 10);
  CHECK(before > 0.01f);

  CHECK(engine.submit(
      audio::AudioCommand::set_core_type(ym2612::CoreType::Nuked)));
  render_ac_peak(engine, 64);
  CHECK(engine.device().core_type() == ym2612::CoreType::Nuked);
  CHECK(engine.notes().published_notes().empty());

  const auto second_note = ym2612::Note::from_midi_note(64);
  CHECK(engine.submit(audio::AudioCommand::note_on(second_note, 127)));
  const float after = render_ac_peak(engine, kSampleRate / 10);
  CHECK(after > 0.01f);
  // The same patch at the same velocity: the two cores must land on the same
  // order of magnitude, or one of them is wired up wrong.
  CHECK(after > before * 0.5f);
  CHECK(after < before * 2.0f);
}

} // namespace

int main() {
  test_sample_rates();
  test_raw_idle_dc_by_chip_type();
  test_nuked_raw_idle_dc_by_chip_type();
  test_nuked_idle_stretch_holds_its_level();
  test_ctrmml_lowpass_response();
  test_idle_settles_to_digital_silence();
  test_apply_patch_reaches_sustaining_note();
  test_apply_patch_updates_instrument_for_future_notes();
  test_apply_patch_preserves_sustaining_note_velocity();
  test_switching_chip_type_releases_notes_and_keeps_audio_working();
  test_switching_core_releases_notes_and_keeps_audio_working();

  AudioEngine engine;
  CHECK(engine.initialize(kSampleRate));
  CHECK(engine.is_running());

  test_key_on_produces_audio(engine);
  test_key_off_decays_to_silence(engine);
  test_retrigger_restarts_envelope(engine);

  std::cout << "All YM2612 render tests passed\n";
  return 0;
}
