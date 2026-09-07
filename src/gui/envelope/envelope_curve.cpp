#include "gui/envelope/envelope_curve.hpp"

#include <algorithm>

namespace ui::envelope {
namespace {

/// The reference note, and the only mutable state in this file. The app writes
/// it whenever the preference changes; every curve built afterwards is drawn
/// at it, and EnvelopeCurveCache notices because it remembers the pitch it
/// last built at.
int g_reference_midi_note = kDefaultReferenceMidiNote;

} // namespace

void set_reference_midi_note(int midi_note) {
  g_reference_midi_note =
      std::clamp(midi_note, kMinReferenceMidiNote, kMaxReferenceMidiNote);
}

int reference_midi_note() { return g_reference_midi_note; }

ym2612_eg::NotePitch reference_pitch() {
  return ym2612_eg::NotePitch::from_midi(g_reference_midi_note);
}

uint8_t packed_ssg(const ym2612::OperatorSettings &op) {
  // Byte for byte what ym2612::Operator::write_settings() puts in $90-$9F.
  // The shape bits are kept even while the enable bit is clear, exactly as the
  // chip receives them; with bit3 down they are inert on hardware and in the
  // simulator alike.
  return static_cast<uint8_t>((op.ssg_enable ? 0x08 : 0x00) |
                              (op.ssg_type_envelope_control & 0x07));
}

ym2612_eg::OperatorParams
to_operator_params(const ym2612::OperatorSettings &op) {
  ym2612_eg::OperatorParams params;
  params.ar = static_cast<uint8_t>(op.attack_rate & 0x1F);
  params.dr = static_cast<uint8_t>(op.decay_rate & 0x1F);
  params.sr = static_cast<uint8_t>(op.sustain_rate & 0x1F);
  params.rr = static_cast<uint8_t>(op.release_rate & 0x0F);
  params.sl = static_cast<uint8_t>(op.sustain_level & 0x0F);
  params.tl = static_cast<uint8_t>(op.total_level & 0x7F);
  params.ks = static_cast<uint8_t>(op.key_scale & 0x03);
  params.ssg = packed_ssg(op);
  return params;
}

EnvelopeCurve build_envelope_curve(const ym2612::OperatorSettings &op) {
  return build_envelope_curve(op, reference_pitch());
}

EnvelopeCurve build_envelope_curve(const ym2612::OperatorSettings &op,
                                   ym2612_eg::NotePitch pitch,
                                   double min_span_ms) {
  return ym2612_eg::graph::build_envelope_curve(to_operator_params(op), pitch,
                                                min_span_ms);
}

} // namespace ui::envelope
