#include "ym2612/operator_edit.hpp"
#include "formats/ym2612_format_adapter.hpp"

#include <algorithm>
#include <utility>

namespace ym2612 {

OperatorFieldRange operator_field_range(OperatorField field) {
  switch (field) {
  case OperatorField::TotalLevel:
    return {0, 127};
  case OperatorField::AttackRate:
  case OperatorField::DecayRate:
  case OperatorField::SustainRate:
    return {0, 31};
  case OperatorField::SustainLevel:
  case OperatorField::ReleaseRate:
  case OperatorField::Multiple:
    return {0, 15};
  case OperatorField::KeyScale:
    return {0, 3};
  case OperatorField::Detune:
    return {0, 6}; // linear -3..+3, not the register encoding
  case OperatorField::SsgType:
    return {0, 7};
  }
  return {0, 0};
}

bool operator_field_is_relative(OperatorField field) {
  // The SSG envelope type names one of eight waveform shapes. Everything
  // else is an amount, and amounts are what a delta is for.
  return field != OperatorField::SsgType;
}

int read_operator_field(const OperatorSettings &op, OperatorField field) {
  switch (field) {
  case OperatorField::TotalLevel:
    return op.total_level;
  case OperatorField::AttackRate:
    return op.attack_rate;
  case OperatorField::DecayRate:
    return op.decay_rate;
  case OperatorField::SustainLevel:
    return op.sustain_level;
  case OperatorField::SustainRate:
    return op.sustain_rate;
  case OperatorField::ReleaseRate:
    return op.release_rate;
  case OperatorField::KeyScale:
    return op.key_scale;
  case OperatorField::Multiple:
    return op.multiple;
  case OperatorField::Detune:
    return formats::adapter::detune_to_linear(op.detune);
  case OperatorField::SsgType:
    return op.ssg_type_envelope_control;
  }
  return 0;
}

void write_operator_field(OperatorSettings &op, OperatorField field,
                          int value) {
  const auto range = operator_field_range(field);
  const auto clamped =
      static_cast<uint8_t>(std::clamp(value, range.min, range.max));

  switch (field) {
  case OperatorField::TotalLevel:
    op.total_level = clamped;
    return;
  case OperatorField::AttackRate:
    op.attack_rate = clamped;
    return;
  case OperatorField::DecayRate:
    op.decay_rate = clamped;
    return;
  case OperatorField::SustainLevel:
    op.sustain_level = clamped;
    return;
  case OperatorField::SustainRate:
    op.sustain_rate = clamped;
    return;
  case OperatorField::ReleaseRate:
    op.release_rate = clamped;
    return;
  case OperatorField::KeyScale:
    op.key_scale = clamped;
    return;
  case OperatorField::Multiple:
    op.multiple = clamped;
    return;
  case OperatorField::Detune:
    op.detune = formats::adapter::detune_from_linear(clamped);
    return;
  case OperatorField::SsgType:
    op.ssg_type_envelope_control = clamped;
    return;
  }
}

void capture_operator_baseline(OperatorEditBaseline &baseline,
                               const ChannelInstrument &instrument,
                               OperatorField field, int primary_slot) {
  baseline.active = true;
  baseline.field = field;
  baseline.primary = primary_slot;
  for (int slot = 0; slot < 4; ++slot) {
    baseline.values[slot] =
        read_operator_field(operator_at(instrument, slot), field);
  }
}

void apply_operator_field_edit(ChannelInstrument &instrument, uint8_t selection,
                               const OperatorEditBaseline &baseline,
                               OperatorField field, int value,
                               MultiEditMode mode) {
  const int primary = baseline.primary;
  const bool baseline_matches =
      baseline.active && baseline.field == field && primary >= 0 && primary < 4;

  if (!baseline_matches) {
    // No drag behind this edit, so there is no delta to spread and no
    // starting point to spread it from. Write the one operator we know.
    if (primary >= 0 && primary < 4) {
      write_operator_field(operator_at(instrument, primary), field, value);
    }
    return;
  }

  write_operator_field(operator_at(instrument, primary), field, value);

  const bool relative =
      mode == MultiEditMode::Relative && operator_field_is_relative(field);
  const int delta = value - baseline.values[primary];

  for (int slot = 0; slot < 4; ++slot) {
    if (slot == primary || (selection & (1u << slot)) == 0) {
      continue;
    }
    // Each operator clamps on its own. Holding the whole group back at the
    // first one to reach an end would make the operator under the cursor
    // stop following it.
    const int target = relative ? baseline.values[slot] + delta : value;
    write_operator_field(operator_at(instrument, slot), field, target);
  }
}

void apply_operator_flag_edit(ChannelInstrument &instrument, uint8_t selection,
                              bool OperatorSettings::*flag, bool value) {
  for (int slot = 0; slot < 4; ++slot) {
    if ((selection & (1u << slot)) == 0) {
      continue;
    }
    operator_at(instrument, slot).*flag = value;
  }
}

OperatorClipboard copy_operator(const ChannelInstrument &instrument, int slot) {
  OperatorClipboard clipboard;
  clipboard.has_value = true;
  clipboard.op = operator_at(instrument, slot);
  if (slot == 0) {
    clipboard.feedback = instrument.feedback;
  }
  return clipboard;
}

void paste_operator(ChannelInstrument &instrument, int slot,
                    const OperatorClipboard &clipboard) {
  if (!clipboard.has_value) {
    return;
  }

  auto &target = operator_at(instrument, slot);
  const bool was_enabled = target.enable;
  target = clipboard.op;
  target.enable = was_enabled;

  if (slot == 0 && clipboard.feedback) {
    instrument.feedback = *clipboard.feedback;
  }
}

void swap_operators(ChannelInstrument &instrument, int lhs_slot,
                    int rhs_slot) {
  if (lhs_slot == rhs_slot) {
    return;
  }

  auto &lhs = operator_at(instrument, lhs_slot);
  auto &rhs = operator_at(instrument, rhs_slot);
  const bool lhs_enable = lhs.enable;
  const bool rhs_enable = rhs.enable;
  std::swap(lhs, rhs);
  lhs.enable = lhs_enable;
  rhs.enable = rhs_enable;
}

} // namespace ym2612
