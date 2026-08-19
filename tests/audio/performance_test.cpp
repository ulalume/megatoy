#include "audio/audio_command.hpp"
#include "audio/performance.hpp"
#include "ym2612/note.hpp"
#include "ym2612/patch.hpp"
#include "ym2612/types.hpp"

#include "../test_check.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

void test_velocity_table() {
  for (int velocity = 0; velocity < 128; ++velocity) {
    const int regenerated =
        velocity == 0
            ? 127
            : std::clamp(static_cast<int>(std::lround(
                             40.0 * std::log10(127.0 / velocity) / 0.75)),
                         0, 127);
    CHECK(ym2612::velocity_tl_offset[velocity] == regenerated);
    if (velocity > 0) {
      CHECK(ym2612::velocity_tl_offset[velocity] <=
            ym2612::velocity_tl_offset[velocity - 1]);
    }
  }

  CHECK(ym2612::velocity_tl_offset[0] == 127);
  CHECK(ym2612::velocity_tl_offset[1] == 112);
  CHECK(ym2612::velocity_tl_offset[127] == 0);
}

void test_velocity_behavior() {
  ym2612::ChannelInstrument instrument;
  instrument.algorithm = 7;
  for (int op = 0; op < 4; ++op) {
    instrument.operators[op].total_level = static_cast<uint8_t>(20 + op);
  }

  const auto full_velocity = instrument.clone_with_velocity(
      audio::performance::effective_velocity(true, 127), 100);
  const auto velocity_disabled = instrument.clone_with_velocity(
      audio::performance::effective_velocity(false, 1), 100);
  CHECK(full_velocity == instrument);
  CHECK(velocity_disabled == full_velocity);
  CHECK(instrument.clone_with_velocity(1, 0) == instrument);

  const auto half_depth = instrument.clone_with_velocity(1, 50);
  const int expected_delta = ym2612::velocity_tl_offset[1] / 2;
  for (int op = 0; op < 4; ++op) {
    CHECK(half_depth.operators[op].total_level ==
          std::min(127, 20 + op + expected_delta));
  }

  instrument.algorithm = 0;
  const auto carrier_only = instrument.clone_with_velocity(1, 100);
  for (int op = 0; op < 3; ++op) {
    CHECK(carrier_only.operators[op].total_level ==
          instrument.operators[op].total_level);
  }
  CHECK(carrier_only.operators[3].total_level == 127);
}

double cents_error(const ym2612::Note &note, float bend,
                   const ym2612::NoteFrequency &frequency) {
  const double base = std::ldexp(
      static_cast<double>(ym2612::fnote_from_key(note.key)), note.octave);
  const double ideal = base * std::exp2(static_cast<double>(bend) / 12.0);
  const double actual =
      std::ldexp(static_cast<double>(frequency.fnum), frequency.block);
  return std::abs(1200.0 * std::log2(actual / ideal));
}

void test_pitch_bend_math() {
  const auto a4 = ym2612::Note::from_midi_note(69);
  const auto center = ym2612::frequency_with_bend(a4, 0.0f);
  const auto up = ym2612::frequency_with_bend(a4, 2.0f);
  const auto down = ym2612::frequency_with_bend(a4, -2.0f);
  CHECK(center.block == 4);
  CHECK(center.fnum == 541);
  CHECK(up.block == 4);
  CHECK(std::abs(static_cast<int>(up.fnum) - 608) <= 1);
  CHECK(down.block == 4);
  CHECK(std::abs(static_cast<int>(down.fnum) - 482) <= 1);

  const auto b4 = ym2612::Note::from_midi_note(71);
  const auto crossed = ym2612::frequency_with_bend(b4, 2.0f);
  CHECK(crossed.block == 5);
  CHECK(std::abs(static_cast<int>(crossed.fnum) - 341) <= 1);

  CHECK(audio::performance::pitch_bend_semitones(8192) == 0.0f);
  CHECK(audio::performance::pitch_bend_semitones(0) == -2.0f);
  CHECK(audio::performance::pitch_bend_semitones(16383) < 2.0f);
  CHECK(audio::performance::pitch_bend_semitones(16383) > 1.999f);

  double maximum_error = 0.0;
  for (uint8_t midi_note = 12; midi_note <= 107; ++midi_note) {
    const auto note = ym2612::Note::from_midi_note(midi_note);
    for (int raw_bend = 0; raw_bend <= 16383; ++raw_bend) {
      const float bend = audio::performance::pitch_bend_semitones(
          static_cast<uint16_t>(raw_bend));
      const auto frequency = ym2612::frequency_with_bend(note, bend);
      maximum_error =
          std::max(maximum_error, cents_error(note, bend, frequency));
    }
  }
  CHECK(maximum_error < 3.1);
}

void test_mod_wheel_composition() {
  CHECK(audio::performance::wheel_pms(0) == 0);
  CHECK(audio::performance::wheel_pms(1) == 1);
  CHECK(audio::performance::wheel_pms(64) == 4);
  CHECK(audio::performance::wheel_pms(127) == 7);

  ym2612::ChannelSettings channel;
  channel.amplitude_modulation_sensitivity = 3;
  channel.frequency_modulation_sensitivity = 5;
  auto effective =
      audio::performance::compose_channel_settings(channel, true, 64);
  CHECK(effective.frequency_modulation_sensitivity == 5);
  CHECK(effective.amplitude_modulation_sensitivity == 3);
  effective = audio::performance::compose_channel_settings(channel, true, 127);
  CHECK(effective.frequency_modulation_sensitivity == 7);
  CHECK(effective.amplitude_modulation_sensitivity == 3);
  effective = audio::performance::compose_channel_settings(channel, false, 64);
  CHECK(effective.frequency_modulation_sensitivity == 5);
  CHECK(effective.amplitude_modulation_sensitivity == 0);

  ym2612::GlobalSettings global;
  global.lfo_enable = false;
  global.lfo_frequency = 7;
  const auto wheel_on =
      audio::performance::compose_global_settings(global, 127);
  CHECK(wheel_on.lfo_enable);
  CHECK(wheel_on.lfo_frequency == audio::performance::kWheelOnlyLfoFrequency);
  const auto wheel_off = audio::performance::compose_global_settings(global, 0);
  CHECK(!wheel_off.lfo_enable);
  CHECK(wheel_off.lfo_frequency == global.lfo_frequency);

  const auto channel_baseline =
      audio::performance::compose_channel_settings(channel, false, 0);
  const auto channel_with_wheel =
      audio::performance::compose_channel_settings(channel, false, 127);
  const auto channel_after_reset =
      audio::performance::compose_channel_settings(channel, false, 0);
  CHECK(channel_after_reset == channel_baseline);
  CHECK(channel_with_wheel.frequency_modulation_sensitivity == 7);
  CHECK(channel_after_reset.frequency_modulation_sensitivity == 5);
  CHECK(channel_after_reset.amplitude_modulation_sensitivity == 0);

  global.lfo_enable = true;
  global.lfo_frequency = 6;
  CHECK(audio::performance::compose_global_settings(global, 0) == global);
  CHECK(audio::performance::compose_channel_settings(channel, true, 0) ==
        channel);
}

void test_performance_commands() {
  const auto bend = audio::AudioCommand::pitch_bend(12345);
  CHECK(bend.type == audio::AudioCommand::Type::PitchBend);
  CHECK(bend.pitch_bend_value == 12345);
  const auto wheel = audio::AudioCommand::mod_wheel(64);
  CHECK(wheel.type == audio::AudioCommand::Type::ModWheel);
  CHECK(wheel.mod_wheel_value == 64);
}

} // namespace

int main() {
  test_velocity_table();
  test_velocity_behavior();
  test_pitch_bend_math();
  test_mod_wheel_composition();
  test_performance_commands();
  std::cout << "All MIDI performance tests passed\n";
  return 0;
}
