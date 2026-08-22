#pragma once

#include "ym2612/types.hpp"
#include <cstdint>
#include <optional>

/**
 * Editing operations on a channel's four operators.
 *
 * Everything here is pure data: no ImGui, no app state. The editor widgets
 * decide *when* to call these; what an edit means -- how a relative change
 * spreads across a selection, what a paste carries, what a swap leaves
 * behind -- is decided here, once, and tested headlessly.
 *
 * Operators are addressed by *display slot*: 0..3 is OP1..OP4 as the editor
 * draws them. That is not the order the chip stores them in, so nothing here
 * indexes ChannelInstrument::operators directly -- see operator_at().
 */
namespace ym2612 {

/// The register index behind a display slot. OP1..OP4 sit at 0, 2, 1, 3.
inline int operator_register_index(int slot) {
  return static_cast<int>(all_operator_indices[static_cast<size_t>(slot)]);
}

inline OperatorSettings &operator_at(ChannelInstrument &instrument, int slot) {
  return instrument.operators[operator_register_index(slot)];
}

inline const OperatorSettings &operator_at(const ChannelInstrument &instrument,
                                           int slot) {
  return instrument.operators[operator_register_index(slot)];
}

/**
 * A numeric operator parameter.
 *
 * Feedback is deliberately absent: it lives on ChannelInstrument, not on an
 * operator, and there is only one of it per channel. The editor draws it
 * inside OP1's panel, which is a layout decision, not a data one.
 */
enum class OperatorField {
  TotalLevel,
  AttackRate,
  DecayRate,
  SustainLevel,
  SustainRate,
  ReleaseRate,
  KeyScale,
  Multiple,
  Detune,
  SsgType,
};

struct OperatorFieldRange {
  int min;
  int max;
};

OperatorFieldRange operator_field_range(OperatorField field);

/**
 * Whether a multi-operator edit spreads the *difference* rather than the
 * value.
 *
 * A delta only means something for fields that are a magnitude. The SSG
 * envelope type is a waveform shape picked out of eight, so nudging it by
 * "+2" is meaningless and it is copied verbatim even in relative mode.
 */
bool operator_field_is_relative(OperatorField field);

/**
 * Read and write in the space the slider works in.
 *
 * Detune is the reason this exists: it is stored sign-magnitude (registers
 * 7, 6, 5 are -3, -2, -1 and 1, 2, 3 are +1, +2, +3), so the stored value is
 * not monotonic in pitch and adding a delta to it produces nonsense. These
 * convert to and from the linear 0..6 the user actually sees.
 */
int read_operator_field(const OperatorSettings &op, OperatorField field);
void write_operator_field(OperatorSettings &op, OperatorField field, int value);

enum class MultiEditMode {
  /// Every selected operator moves by the same amount. Preserves the shape
  /// of the relationship between them.
  Relative,
  /// Every selected operator lands on the same value.
  Absolute,
};

/**
 * What the selected operators held when the drag began.
 *
 * A relative edit is `baseline + delta`, never `current + delta`. Feeding
 * per-frame deltas forward lets an operator that hits the end of its range
 * drift: drag total level up until a quiet operator clamps at 127, drag back
 * down, and it comes to rest somewhere it never was. Anchoring every frame
 * to the value the drag started from makes a round trip lossless.
 */
struct OperatorEditBaseline {
  bool active = false;
  OperatorField field = OperatorField::TotalLevel;
  int primary = -1;
  int values[4] = {0, 0, 0, 0};

  void clear() {
    active = false;
    primary = -1;
  }
};

void capture_operator_baseline(OperatorEditBaseline &baseline,
                               const ChannelInstrument &instrument,
                               OperatorField field, int primary_slot);

/**
 * Write `value` into `primary_slot` and spread it across `selection`.
 *
 * `selection` is a bit per display slot. Operators whose `enable` is off are
 * written like any other: the flag mutes the operator, it does not archive
 * its settings.
 *
 * The baseline is only an anchor for the spreading. If it does not belong to
 * this field and this operator -- an edit that arrived without a drag behind
 * it, or after the drag ended -- the edited operator is still written and
 * the others are left alone, because there is no starting point to measure a
 * delta from.
 */
void apply_operator_field_edit(ChannelInstrument &instrument, uint8_t selection,
                               const OperatorEditBaseline &baseline,
                               OperatorField field, int primary_slot, int value,
                               MultiEditMode mode);

/**
 * Spread a boolean across the selection.
 *
 * Booleans have no meaningful delta, so this is absolute in both modes.
 * `enable` never goes through here: see paste_operator().
 */
void apply_operator_flag_edit(ChannelInstrument &instrument, uint8_t selection,
                              bool OperatorSettings::*flag, bool value);

/**
 * One operator on the editor's own clipboard.
 *
 * Not the system clipboard: the web build can be embedded in an iframe where
 * clipboard access is denied outright, and a feature that silently stops
 * working there is worse than one that never reached across processes.
 */
struct OperatorClipboard {
  bool has_value = false;
  OperatorSettings op;
  /**
   * Set only when the copy came from OP1, the slot the editor draws the
   * channel's feedback in. Pasting anywhere else drops it -- there is
   * nothing on OP2..OP4 for it to land on.
   */
  std::optional<uint8_t> feedback;
};

OperatorClipboard copy_operator(const ChannelInstrument &instrument, int slot);

/**
 * Overwrite `slot` with the clipboard, leaving its `enable` alone.
 *
 * `enable` is a mute, not part of the sound: silencing three operators to
 * hear the fourth is a listening aid, and a paste that switched them back on
 * would undo the thing the user set up to do the paste in the first place.
 */
void paste_operator(ChannelInstrument &instrument, int slot,
                    const OperatorClipboard &clipboard);

/**
 * Exchange two operators, leaving both `enable` flags where they are, for
 * the same reason paste does not carry it: whichever operators were muted
 * before the swap stay muted after it.
 *
 * Feedback does not move. It belongs to the channel, and there is only one.
 */
void swap_operators(ChannelInstrument &instrument, int lhs_slot, int rhs_slot);

} // namespace ym2612
