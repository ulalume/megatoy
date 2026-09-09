#pragma once

/**
 * The envelope graph in megatoy's own terms. ym2612_eg::graph decides
 * everything about the drawing from ym2612_eg::OperatorParams; the two things
 * it cannot know are here: how megatoy's register struct maps onto those
 * params, and the single note every graph is drawn at.
 */

#include "ym2612/types.hpp"

#include <ym2612_eg/ym2612_eg.hpp>

#include <cstddef>
#include <cstdint>

namespace ui::envelope {

using ym2612_eg::graph::EnvelopeCurve;
using ym2612_eg::graph::VoiceCursor;

using ym2612_eg::graph::cursor_for_voice;
using ym2612_eg::graph::grid_step_ms;
using ym2612_eg::graph::release_max_ms;
using ym2612_eg::graph::same_envelope;

/// The single reference note every envelope graph is drawn at, and the only
/// place the note is decided: everything downstream asks reference_pitch(),
/// and the app pushes the preference in through set_reference_midi_note().
inline constexpr int kDefaultReferenceMidiNote = 60; // middle C
/// C0 to B7: megatoy's own note range, one F-num block each. Above B7 the
/// chip has no block left and every note folds onto the same pitch.
inline constexpr int kMinReferenceMidiNote = 12;  // C0
inline constexpr int kMaxReferenceMidiNote = 107; // B7

/// Clamped into [kMinReferenceMidiNote, kMaxReferenceMidiNote].
void set_reference_midi_note(int midi_note);
int reference_midi_note();
ym2612_eg::NotePitch reference_pitch();

/// megatoy stores the SSG-EG enable bit and the 3-bit shape separately; the
/// chip register (and ym2612_eg) wants one nibble:
/// bit3 enable, bit2 attack, bit1 alternate, bit0 hold.
uint8_t packed_ssg(const ym2612::OperatorSettings &op);

ym2612_eg::OperatorParams
to_operator_params(const ym2612::OperatorSettings &op);

/// The curve at the reference note.
EnvelopeCurve build_envelope_curve(const ym2612::OperatorSettings &op);

/// The same curve at an arbitrary note, for a voice that is actually sounding.
/// `min_span_ms` is the axis the curve will be DRAWN on -- the reference
/// curve's, not its own.
EnvelopeCurve build_envelope_curve(const ym2612::OperatorSettings &op,
                                   ym2612_eg::NotePitch pitch,
                                   double min_span_ms = 0.0);

/// One operator's curve, at whatever the reference note currently is.
class EnvelopeCurveCache {
public:
  const EnvelopeCurve &get(const ym2612::OperatorSettings &op) {
    return impl_.get(to_operator_params(op), reference_pitch());
  }

  using Clock = ym2612_eg::graph::EnvelopeCurveCache::Clock;
  void set_clock(Clock clock) { impl_.set_clock(clock); }

  int rebuild_count() const { return impl_.rebuild_count(); }

private:
  ym2612_eg::graph::EnvelopeCurveCache impl_;
};

/// The curves of the notes being played, on the reference curve's axis.
class VoiceCurveCache {
public:
  static constexpr std::size_t kMaxEntries =
      ym2612_eg::graph::VoiceCurveCache::kMaxEntries;

  const EnvelopeCurve *get(const ym2612::OperatorSettings &op,
                           ym2612_eg::NotePitch pitch,
                           const EnvelopeCurve &reference, int &build_budget) {
    return impl_.get(to_operator_params(op), pitch, reference,
                     reference_pitch(), build_budget);
  }

  int rebuild_count() const { return impl_.rebuild_count(); }
  std::size_t size() const { return impl_.size(); }

private:
  ym2612_eg::graph::VoiceCurveCache impl_;
};

} // namespace ui::envelope
