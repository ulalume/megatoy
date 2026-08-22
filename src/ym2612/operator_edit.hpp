#pragma once

#include "ym2612/types.hpp"
#include <cstdint>
#include <optional>

/**
 * Editing operations on a channel's four operators. Pure data, so what an
 * edit means is decided here once and tested headlessly.
 *
 * Operators are addressed by display slot: 0..3 is OP1..OP4 as drawn, which
 * is not the order the chip stores them in -- see operator_at().
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
 * A numeric operator parameter. Feedback is absent on purpose: it belongs to
 * the channel, and OP1's panel is only where it is drawn.
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
 * Whether a multi-operator edit spreads the difference rather than the value.
 * The SSG envelope type is a shape, not a magnitude, so it is copied verbatim
 * even in relative mode.
 */
bool operator_field_is_relative(OperatorField field);

/**
 * Read and write in the space the slider works in. Detune is why: it is
 * stored sign-magnitude, so it is not monotonic in pitch and a delta added to
 * the stored value is nonsense. These use the linear 0..6 the user sees.
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
 * A relative edit is `baseline + delta`, never `current + delta`: accumulated
 * deltas let an operator that clamps at the end of its range come to rest
 * somewhere it never was when the drag comes back.
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
 * Write `value` into `primary_slot` and spread it across `selection`, a bit
 * per display slot. Disabled operators are written like any other -- the flag
 * mutes, it does not archive.
 *
 * A baseline belonging to another field or operator spreads nothing; the
 * edited operator is still written.
 */
void apply_operator_field_edit(ChannelInstrument &instrument, uint8_t selection,
                               const OperatorEditBaseline &baseline,
                               OperatorField field, int primary_slot, int value,
                               MultiEditMode mode);

/// Spread a boolean across the selection. Absolute in both modes; `enable`
/// never goes through here.
void apply_operator_flag_edit(ChannelInstrument &instrument, uint8_t selection,
                              bool OperatorSettings::*flag, bool value);

/**
 * One operator on the editor's own clipboard. Not the system one: the web
 * build can be embedded where clipboard access is denied outright.
 */
struct OperatorClipboard {
  bool has_value = false;
  OperatorSettings op;
  /// Set only for a copy from OP1, the slot feedback is drawn in. Pasting
  /// anywhere else drops it.
  std::optional<uint8_t> feedback;
};

OperatorClipboard copy_operator(const ChannelInstrument &instrument, int slot);

/**
 * Overwrite `slot`, leaving its `enable` alone: it is a mute, and restoring
 * it would undo the audition the paste was set up for.
 */
void paste_operator(ChannelInstrument &instrument, int slot,
                    const OperatorClipboard &clipboard);

/**
 * Exchange two operators. `enable` stays put for the same reason paste leaves
 * it, and feedback does not move -- it belongs to the channel.
 */
void swap_operators(ChannelInstrument &instrument, int lhs_slot, int rhs_slot);

} // namespace ym2612
