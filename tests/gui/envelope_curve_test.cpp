#include "../test_check.hpp"
#include "gui/envelope/envelope_curve.hpp"

#include <cstdint>
#include <iostream>

namespace {

using namespace ui::envelope;

ym2612::OperatorSettings adsr(int ar, int dr, int sl, int sr, int rr, int ks) {
  ym2612::OperatorSettings op;
  op.attack_rate = static_cast<uint8_t>(ar);
  op.decay_rate = static_cast<uint8_t>(dr);
  op.sustain_level = static_cast<uint8_t>(sl);
  op.sustain_rate = static_cast<uint8_t>(sr);
  op.release_rate = static_cast<uint8_t>(rr);
  op.key_scale = static_cast<uint8_t>(ks);
  return op;
}

/// EG_SPEC's worked example: AR=31 TL=0 DR=10 SL=2 SR=5 RR=7, KS=0.
ym2612::OperatorSettings worked_example() { return adsr(31, 10, 2, 5, 7, 0); }

/// A clock that leaps a whole second every time it is read, which is past
/// every interval the cache's rebuild throttle can ask for.
double g_leaping_ms = 0.0;
double leaping_clock() {
  g_leaping_ms += 1000.0;
  return g_leaping_ms;
}

// ------------------------------------------------------- params conversion

void test_registers_map_straight_through() {
  ym2612::OperatorSettings op = worked_example();
  op.key_scale = 2;
  const auto params = to_operator_params(op);
  CHECK(params.ar == 31);
  CHECK(params.dr == 10);
  CHECK(params.sr == 5);
  CHECK(params.rr == 7);
  CHECK(params.sl == 2);
  CHECK(params.tl == 0);
  CHECK(params.ks == 2);
  CHECK(params.ssg == 0);
}

void test_ssg_bits_are_packed_the_way_the_chip_wants_them() {
  ym2612::OperatorSettings op;

  // Disabled: the shape bits still go out and are inert while bit3 is clear.
  op.ssg_enable = false;
  op.ssg_type_envelope_control = 5;
  CHECK(packed_ssg(op) == 0x05);
  CHECK((packed_ssg(op) & 0x08) == 0);

  // bit3 enable, bit2 attack, bit1 alternate, bit0 hold.
  op.ssg_enable = true;
  op.ssg_type_envelope_control = 0;
  CHECK(packed_ssg(op) == 0x08);
  op.ssg_type_envelope_control = 1; // hold
  CHECK(packed_ssg(op) == 0x09);
  op.ssg_type_envelope_control = 2; // alternate
  CHECK(packed_ssg(op) == 0x0A);
  op.ssg_type_envelope_control = 4; // attack
  CHECK(packed_ssg(op) == 0x0C);
  op.ssg_type_envelope_control = 7; // attack + alternate + hold
  CHECK(packed_ssg(op) == 0x0F);

  for (int type = 0; type < 8; ++type) {
    op.ssg_type_envelope_control = static_cast<uint8_t>(type);
    CHECK(to_operator_params(op).ssg == 0x08 + type);
  }
}

/// The fields the translation deliberately drops: multiple, detune and the
/// rest do not shape an envelope.
void test_only_envelope_registers_count_as_a_change() {
  const ym2612::OperatorSettings a = worked_example();
  ym2612::OperatorSettings b = a;
  b.multiple = 7;
  b.detune = 3;
  b.amplitude_modulation_enable = true;
  CHECK(same_envelope(to_operator_params(a), to_operator_params(b)));

  b = a;
  b.sustain_rate = 6;
  CHECK(!same_envelope(to_operator_params(a), to_operator_params(b)));

  EnvelopeCurveCache cache;
  cache.set_clock(&leaping_clock);
  ym2612::OperatorSettings op = a;
  cache.get(op);
  CHECK(cache.rebuild_count() == 1);
  op.multiple = 4;
  cache.get(op);
  CHECK(cache.rebuild_count() == 1);
  op.decay_rate = 12;
  cache.get(op);
  CHECK(cache.rebuild_count() == 2);
}

// -------------------------------------------------------- reference note

void test_the_reference_note_is_a_setting() {
  const auto middle_c = ym2612_eg::NotePitch::from_midi(60);
  CHECK(reference_midi_note() == kDefaultReferenceMidiNote);
  CHECK(reference_pitch().fnum == middle_c.fnum);
  CHECK(reference_pitch().block == middle_c.block);

  set_reference_midi_note(72); // C5
  CHECK(reference_midi_note() == 72);
  CHECK(reference_pitch().block == 5);
  CHECK(reference_pitch().fnum == ym2612_eg::NotePitch::from_midi(72).fnum);

  // The preference is a plain integer on disk, so it is clamped rather than
  // trusted.
  set_reference_midi_note(-1);
  CHECK(reference_midi_note() == kMinReferenceMidiNote);
  set_reference_midi_note(1000);
  CHECK(reference_midi_note() == kMaxReferenceMidiNote);

  set_reference_midi_note(kDefaultReferenceMidiNote);
}

/// The same registers decay far faster high up the keyboard, and the curve
/// built without a pitch is built at the reference note.
void test_key_scaling_follows_the_reference_note() {
  ym2612::OperatorSettings op = worked_example();
  op.key_scale = 3;

  set_reference_midi_note(36); // C2
  const double low = build_envelope_curve(op).decay_end_ms;
  set_reference_midi_note(96); // C7
  const double high = build_envelope_curve(op).decay_end_ms;
  set_reference_midi_note(kDefaultReferenceMidiNote);

  CHECK(low > 0.0);
  CHECK(high > 0.0);
  CHECK(high < low * 0.5);

  // The explicit-pitch overload answers the same thing without the setting.
  CHECK(build_envelope_curve(op, ym2612_eg::NotePitch::from_midi(36))
            .decay_end_ms == low);
  CHECK(build_envelope_curve(op, ym2612_eg::NotePitch::from_midi(96))
            .decay_end_ms == high);
}

/// The note is part of what a cached curve was built at, so moving it
/// invalidates every operator's curve.
void test_the_cache_rebuilds_when_the_reference_note_changes() {
  EnvelopeCurveCache cache;
  cache.set_clock(&leaping_clock);
  ym2612::OperatorSettings op = worked_example();
  op.key_scale = 3;

  cache.get(op);
  cache.get(op);
  CHECK(cache.rebuild_count() == 1);

  set_reference_midi_note(84); // C6
  cache.get(op);
  cache.get(op);
  CHECK(cache.rebuild_count() == 2);

  set_reference_midi_note(kDefaultReferenceMidiNote);
  cache.get(op);
  CHECK(cache.rebuild_count() == 3);
}

/// A voice at the reference note is drawn by the curve already on screen
/// rather than by one of its own.
void test_a_voice_at_the_reference_note_reuses_the_reference_curve() {
  ym2612::OperatorSettings op = worked_example();
  op.key_scale = 3; // every block is its own key-scale value

  set_reference_midi_note(kMinReferenceMidiNote); // C0
  const EnvelopeCurve reference = build_envelope_curve(op);

  VoiceCurveCache cache;
  int budget = 1;
  CHECK(cache.get(op, reference_pitch(), reference, budget) == &reference);
  CHECK(budget == 1);
  CHECK(cache.rebuild_count() == 0);

  // Any other note is its own entry, simulated on the reference curve's axis.
  const EnvelopeCurve *voice =
      cache.get(op, ym2612_eg::NotePitch::from_midi(96), reference, budget);
  CHECK(voice != nullptr);
  CHECK(voice != &reference);
  CHECK(budget == 0);
  CHECK(cache.rebuild_count() == 1);
  CHECK(cache.size() == 1);
  CHECK(cache.size() <= VoiceCurveCache::kMaxEntries);

  set_reference_midi_note(kDefaultReferenceMidiNote);
}

} // namespace

int main() {
  test_registers_map_straight_through();
  test_ssg_bits_are_packed_the_way_the_chip_wants_them();
  test_only_envelope_registers_count_as_a_change();

  test_the_reference_note_is_a_setting();
  test_key_scaling_follows_the_reference_note();
  test_the_cache_rebuilds_when_the_reference_note_changes();
  test_a_voice_at_the_reference_note_reuses_the_reference_curve();

  std::cout << "envelope_curve_test passed\n";
  return 0;
}
