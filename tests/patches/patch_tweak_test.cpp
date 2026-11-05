#include "../test_check.hpp"
#include "patches/patch_tweak.hpp"

#include <iostream>
#include <string>

namespace {

using patch_lab::Tweak;
using patch_lab::tweak_fields;
using patch_lab::tweak_range;
using patch_lab::tweak_text;
using patch_lab::tweak_value;
using patch_lab::with_tweak;
using ym2612::ChannelInstrument;

// Operators in storage order: S1, S3, S2, S4.
ChannelInstrument make_instrument(uint8_t algorithm) {
  ChannelInstrument instrument;
  instrument.algorithm = algorithm;
  for (auto &op : instrument.operators) {
    op.attack_rate = 31;
    op.decay_rate = 10;
    op.sustain_level = 4;
    op.sustain_rate = 6;
    op.release_rate = 8;
    op.total_level = 20;
  }
  return instrument;
}

void test_brightness_moves_the_modulators_together() {
  auto base = make_instrument(4); // S1 and S3 modulate, S2 and S4 are carriers.
  base.operators[0].total_level = 30;
  base.operators[1].total_level = 50;
  CHECK(tweak_value(base, Tweak::Brightness) == 97);
  CHECK(tweak_text(base, Tweak::Brightness) == "TL 30");
  CHECK(tweak_text(base, Tweak::Attack) == "AR 31");
  CHECK(patch_lab::tweak_change_text(base, base, Tweak::Attack) == "AR 31");

  const auto brighter = with_tweak(base, Tweak::Brightness, 107);
  CHECK(brighter.operators[0].total_level == 20);
  CHECK(brighter.operators[1].total_level == 40);
  CHECK(brighter.operators[2].total_level == 20);
  CHECK(brighter.operators[3].total_level == 20);

  const auto top = with_tweak(base, Tweak::Brightness, 127);
  CHECK(top.operators[0].total_level == 0);
  CHECK(top.operators[1].total_level == 20);

  // An operator stopping at the end loses nothing while the base is kept.
  const auto dark = with_tweak(base, Tweak::Brightness, 0);
  CHECK(dark.operators[0].total_level == 127);
  CHECK(dark.operators[1].total_level == 127);
  CHECK(with_tweak(base, Tweak::Brightness, 97) == base);
}

void test_brightness_needs_a_modulator() {
  auto base = make_instrument(7);
  base.feedback = 2;
  CHECK(!tweak_value(base, Tweak::Brightness));
  CHECK(with_tweak(base, Tweak::Brightness, 5) == base);
  CHECK(tweak_text(base, Tweak::Brightness) == "-");
  for (const auto &fields : tweak_fields(base, Tweak::Brightness)) {
    CHECK(!fields.total_level);
  }
}

void test_fields_are_the_ones_a_tweak_moves() {
  auto base = make_instrument(4); // S1 and S3 modulate, S2 and S4 are carriers.
  base.operators[3].enable = false;
  base.operators[1].sustain_rate = 0;

  const auto volume = tweak_fields(base, Tweak::Volume);
  CHECK(!volume[0].total_level && !volume[1].total_level);
  CHECK(volume[2].total_level && !volume[3].total_level);
  CHECK(!volume[2].attack_rate);

  const auto brightness = tweak_fields(base, Tweak::Brightness);
  CHECK(brightness[0].total_level && brightness[1].total_level);
  CHECK(!brightness[2].total_level);

  const auto decay = tweak_fields(base, Tweak::Decay);
  CHECK(decay[0].decay_rate && decay[0].sustain_level);
  CHECK(!decay[3].decay_rate);

  // A held note stays held, so its sustain rate is not moved.
  const auto release = tweak_fields(base, Tweak::Release);
  CHECK(release[0].release_rate && release[0].sustain_rate);
  CHECK(release[1].release_rate && !release[1].sustain_rate);
}

void test_volume_keeps_the_carriers_in_balance() {
  auto base = make_instrument(4);
  base.operators[2].total_level = 10;
  base.operators[3].total_level = 40;
  CHECK(tweak_value(base, Tweak::Volume) == 117);
  CHECK(tweak_text(base, Tweak::Volume) == "TL 10");

  const auto loud = with_tweak(base, Tweak::Volume, 127);
  CHECK(loud.operators[2].total_level == 0);
  CHECK(loud.operators[3].total_level == 30);
  CHECK(loud.operators[0].total_level == 20);
  CHECK(tweak_text(loud, Tweak::Volume) == "TL 0");
  CHECK(patch_lab::tweak_change_text(base, loud, Tweak::Volume) == "+7.5 dB");
  CHECK(patch_lab::tweak_change_text(loud, base, Tweak::Volume) == "-7.5 dB");
  CHECK(patch_lab::tweak_change_text(base, base, Tweak::Volume) == "+0.0 dB");

  // Both stop once the quieter one is out of room, short of silence.
  const auto quiet = with_tweak(base, Tweak::Volume, 0);
  CHECK(quiet.operators[2].total_level == 96);
  CHECK(quiet.operators[3].total_level == 126);
  CHECK(tweak_value(quiet, Tweak::Volume) == 31);
}

void test_volume_leaves_a_silent_carrier_silent() {
  auto base = make_instrument(4);
  base.operators[2].total_level = 0;
  base.operators[3].total_level = 127;
  CHECK(tweak_value(base, Tweak::Volume) == 127);

  const auto quieter = with_tweak(base, Tweak::Volume, 100);
  CHECK(quieter.operators[2].total_level == 27);
  CHECK(quieter.operators[3].total_level == 127);
  CHECK(tweak_value(quieter, Tweak::Volume) == 100);

  // Down as far as it goes and up again, the silent one stays silent.
  const auto bottom = with_tweak(base, Tweak::Volume, 0);
  CHECK(bottom.operators[2].total_level == 126);
  const auto back = with_tweak(bottom, Tweak::Volume, 127);
  CHECK(back.operators[2].total_level == 0);
  CHECK(back.operators[3].total_level == 127);
  // Volume lights only the carrier it moves.
  const auto fields = tweak_fields(base, Tweak::Volume);
  CHECK(fields[2].total_level && !fields[3].total_level);

  // With every carrier silent, they all come up together.
  auto silent = base;
  silent.operators[2].total_level = 127;
  const auto louder = with_tweak(silent, Tweak::Volume, 27);
  CHECK(louder.operators[2].total_level == 100);
  CHECK(louder.operators[3].total_level == 100);
}

void test_shape_follows_the_carriers() {
  auto base = make_instrument(4);
  base.operators[2].total_level = 0;
  base.operators[3].total_level = 64;
  const auto shape = patch_lab::tweak_shape(base);
  CHECK(shape.has_value());
  CHECK(shape->level == 1.0f);
  CHECK(shape->attack == 0.0f);

  // Longer and quieter as the tweaks move that way.
  const auto slower =
      patch_lab::tweak_shape(with_tweak(base, Tweak::Attack, 10));
  CHECK(slower->attack > shape->attack);
  const auto quieter =
      patch_lab::tweak_shape(with_tweak(base, Tweak::Volume, 60));
  CHECK(quieter->level < shape->level);
  const auto longer =
      patch_lab::tweak_shape(with_tweak(base, Tweak::Release, 2));
  CHECK(longer->release > shape->release);

  auto none = base;
  none.operators[2].enable = false;
  none.operators[3].enable = false;
  CHECK(!patch_lab::tweak_shape(none));
}

void test_attack_follows_the_fastest_carrier_and_keeps_sounding() {
  auto base = make_instrument(4);
  base.operators[0].attack_rate = 20;
  base.operators[2].attack_rate = 25;
  base.operators[3].attack_rate = 10;
  CHECK(tweak_value(base, Tweak::Attack) == 25);

  const auto slower = with_tweak(base, Tweak::Attack, 15);
  CHECK(slower.operators[2].attack_rate == 15);
  CHECK(slower.operators[3].attack_rate == 4);
  CHECK(slower.operators[0].attack_rate == 10);

  CHECK(tweak_range(base, Tweak::Attack).min == 4);
  const auto floor = with_tweak(base, Tweak::Attack, 0);
  CHECK(floor.operators[2].attack_rate == 4);

  // A patch already slower than the limit is left reachable, not raised.
  auto slow = make_instrument(7);
  for (auto &op : slow.operators) {
    op.attack_rate = 2;
  }
  CHECK(tweak_range(slow, Tweak::Attack).min == 2);
  CHECK(with_tweak(slow, Tweak::Attack, 2) == slow);
}

void test_decay_moves_rate_and_level_as_a_pair() {
  auto base = make_instrument(7);
  base.operators[3].sustain_level = 2;
  base.operators[3].decay_rate = 6;
  CHECK(tweak_value(base, Tweak::Decay) == 10);
  CHECK(tweak_text(base, Tweak::Decay) == "DR 6  SL 2");

  const auto shorter = with_tweak(base, Tweak::Decay, 14);
  CHECK(shorter.operators[3].decay_rate == 8);
  CHECK(shorter.operators[3].sustain_level == 3);
  CHECK(shorter.operators[0].decay_rate == 12);
  CHECK(shorter.operators[0].sustain_level == 5);

  const auto held = with_tweak(base, Tweak::Decay, 0);
  CHECK(held.operators[3].decay_rate == 0);
  CHECK(held.operators[3].sustain_level == 0);

  const auto shortest = with_tweak(base, Tweak::Decay, 61);
  CHECK(shortest.operators[3].decay_rate == 31);
  CHECK(shortest.operators[3].sustain_level == 15);
}

void test_release_moves_both_tails_and_leaves_a_held_note_held() {
  auto base = make_instrument(7);
  base.operators[0].sustain_rate = 0;
  base.operators[1].sustain_rate = 3;
  CHECK(tweak_value(base, Tweak::Release) == 8);
  CHECK(tweak_text(base, Tweak::Release) == "RR 8  SR 0");

  const auto shorter = with_tweak(base, Tweak::Release, 10);
  CHECK(shorter.operators[0].release_rate == 10);
  CHECK(shorter.operators[0].sustain_rate == 0);
  CHECK(shorter.operators[1].sustain_rate == 7);

  const auto longer = with_tweak(base, Tweak::Release, 4);
  CHECK(longer.operators[0].sustain_rate == 0);
  CHECK(longer.operators[1].sustain_rate == 1);
  CHECK(longer.operators[2].sustain_rate == 1);
}

void test_disabled_operators_stay_as_they_are() {
  auto base = make_instrument(4);
  base.operators[0].enable = false;
  base.operators[3].enable = false;
  const auto moved = with_tweak(base, Tweak::Attack, 20);
  CHECK(moved.operators[0].attack_rate == 31);
  CHECK(moved.operators[3].attack_rate == 31);
  CHECK(moved.operators[1].attack_rate == 20);
  const auto quiet = with_tweak(base, Tweak::Volume, 50);
  CHECK(quiet.operators[3].total_level == 20);
  CHECK(quiet.operators[2].total_level == 77);

  auto none = make_instrument(7);
  for (auto &op : none.operators) {
    op.enable = false;
  }
  for (const auto tweak : {Tweak::Volume, Tweak::Brightness, Tweak::Attack,
                           Tweak::Decay, Tweak::Release}) {
    CHECK(!tweak_value(none, tweak));
    CHECK(with_tweak(none, tweak, 3) == none);
  }
}

void test_every_algorithm_reads_back_what_was_set() {
  for (uint8_t algorithm = 0; algorithm < 8; ++algorithm) {
    auto base = make_instrument(algorithm);
    for (std::size_t i = 0; i < 4; ++i) {
      base.operators[i].total_level = static_cast<uint8_t>(12 + i * 3);
      base.operators[i].attack_rate = static_cast<uint8_t>(24 + i);
      base.operators[i].decay_rate = static_cast<uint8_t>(9 + i);
      base.operators[i].release_rate = static_cast<uint8_t>(6 + i);
    }
    for (const auto tweak : {Tweak::Volume, Tweak::Brightness, Tweak::Attack,
                             Tweak::Decay, Tweak::Release}) {
      if (tweak == Tweak::Brightness && algorithm == 7) {
        CHECK(!tweak_value(base, tweak));
        continue;
      }
      CHECK(with_tweak(base, tweak, *tweak_value(base, tweak)) == base);
      const auto range = tweak_range(base, tweak);
      int last = *tweak_value(with_tweak(base, tweak, range.min), tweak);
      for (int value = range.min; value <= range.max; ++value) {
        const int now = *tweak_value(with_tweak(base, tweak, value), tweak);
        CHECK(now >= last);
        last = now;
      }
    }
  }
}

} // namespace

int main() {
  test_brightness_moves_the_modulators_together();
  test_brightness_needs_a_modulator();
  test_fields_are_the_ones_a_tweak_moves();
  test_volume_keeps_the_carriers_in_balance();
  test_volume_leaves_a_silent_carrier_silent();
  test_shape_follows_the_carriers();
  test_attack_follows_the_fastest_carrier_and_keeps_sounding();
  test_decay_moves_rate_and_level_as_a_pair();
  test_release_moves_both_tails_and_leaves_a_held_note_held();
  test_disabled_operators_stay_as_they_are();
  test_every_algorithm_reads_back_what_was_set();
  std::cout << "All patch tweak tests passed\n";
  return 0;
}
