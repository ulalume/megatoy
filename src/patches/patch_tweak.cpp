#include "patches/patch_tweak.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

namespace patch_lab {
namespace {

using ym2612::ChannelInstrument;
using ym2612::OperatorSettings;

// Below this an attack takes seconds, and at 0 the operator never sounds.
constexpr int kMinAttack = 4;
constexpr int kMaxDecay = 31 + 15 * 2;
constexpr int kMaxShift = 127;
constexpr double kDecibelsPerTotalLevel = 0.75;
constexpr int kSilentTotalLevel = 127;

bool is_carrier(const ChannelInstrument &instrument, std::size_t index) {
  return index >= ym2612::algorithm_modulator_count[instrument.algorithm];
}

/// The enabled carrier `better` prefers over all others.
template <typename Better>
const OperatorSettings *pick_carrier(const ChannelInstrument &instrument,
                                     Better better) {
  const OperatorSettings *picked = nullptr;
  for (std::size_t i = 0; i < 4; ++i) {
    const auto &op = instrument.operators[i];
    if (op.enable && is_carrier(instrument, i) &&
        (picked == nullptr || better(op, *picked))) {
      picked = &op;
    }
  }
  return picked;
}

const OperatorSettings *loudest_carrier(const ChannelInstrument &instrument) {
  return pick_carrier(instrument,
                      [](const OperatorSettings &a, const OperatorSettings &b) {
                        return a.total_level < b.total_level;
                      });
}

const OperatorSettings *fastest_carrier(const ChannelInstrument &instrument) {
  return pick_carrier(instrument,
                      [](const OperatorSettings &a, const OperatorSettings &b) {
                        return a.attack_rate > b.attack_rate;
                      });
}

/// The carrier that holds its level longest once its attack is over.
const OperatorSettings *held_carrier(const ChannelInstrument &instrument) {
  return pick_carrier(instrument,
                      [](const OperatorSettings &a, const OperatorSettings &b) {
                        return a.sustain_level != b.sustain_level
                                   ? a.sustain_level < b.sustain_level
                                   : a.decay_rate < b.decay_rate;
                      });
}

const OperatorSettings *longest_release(const ChannelInstrument &instrument) {
  return pick_carrier(instrument,
                      [](const OperatorSettings &a, const OperatorSettings &b) {
                        return a.release_rate < b.release_rate;
                      });
}

const OperatorSettings *
strongest_modulator(const ChannelInstrument &instrument) {
  const OperatorSettings *picked = nullptr;
  for (std::size_t i = 0; i < 4; ++i) {
    const auto &op = instrument.operators[i];
    if (op.enable && !is_carrier(instrument, i) &&
        (picked == nullptr || op.total_level < picked->total_level)) {
      picked = &op;
    }
  }
  return picked;
}

uint8_t moved(int value, int shift, int low, int high) {
  return static_cast<uint8_t>(std::clamp(value + shift, low, high));
}

void shift_volume(ChannelInstrument &instrument, int shift) {
  // A silent carrier adds nothing to the mix, so it stays silent and stops
  // nothing -- unless every carrier is silent, when they all come up.
  bool any_audible = false;
  for (std::size_t i = 0; i < 4; ++i) {
    const auto &op = instrument.operators[i];
    any_audible |= op.enable && is_carrier(instrument, i) &&
                   op.total_level < kSilentTotalLevel;
  }
  const auto moves = [&](std::size_t i) {
    const auto &op = instrument.operators[i];
    return op.enable && is_carrier(instrument, i) &&
           (!any_audible || op.total_level < kSilentTotalLevel);
  };

  // Carriers stay in balance: all of them stop when the first one reaches an
  // end, since the mix between them is part of the sound.
  int quietest = 0;
  int loudest = kSilentTotalLevel;
  for (std::size_t i = 0; i < 4; ++i) {
    if (moves(i)) {
      quietest = std::max<int>(quietest, instrument.operators[i].total_level);
      loudest = std::min<int>(loudest, instrument.operators[i].total_level);
    }
  }
  const int amount = std::clamp(shift, quietest - kSilentTotalLevel, loudest);
  for (std::size_t i = 0; i < 4; ++i) {
    if (moves(i)) {
      auto &op = instrument.operators[i];
      op.total_level = static_cast<uint8_t>(op.total_level - amount);
    }
  }
}

void shift_brightness(ChannelInstrument &instrument, int shift) {
  for (std::size_t i = 0; i < 4; ++i) {
    auto &op = instrument.operators[i];
    if (op.enable && !is_carrier(instrument, i)) {
      op.total_level = moved(op.total_level, -shift, 0, 127);
    }
  }
}

void shift_envelopes(ChannelInstrument &instrument, Tweak tweak, int shift) {
  for (auto &op : instrument.operators) {
    if (!op.enable) {
      continue;
    }
    switch (tweak) {
    case Tweak::Attack:
      op.attack_rate = moved(op.attack_rate, shift,
                             std::min<int>(kMinAttack, op.attack_rate), 31);
      break;
    case Tweak::Decay:
      // Two steps of a rate halve its time, as one step of level drops 3 dB.
      op.decay_rate = moved(op.decay_rate, shift, 0, 31);
      op.sustain_level = moved(op.sustain_level, shift / 2, 0, 15);
      break;
    case Tweak::Release:
      // Release runs at twice the rate per step. A sustain rate of 0 means the
      // note holds, which stays true whichever way this moves.
      op.release_rate = moved(op.release_rate, shift, 0, 15);
      if (op.sustain_rate != 0) {
        op.sustain_rate = moved(op.sustain_rate, shift * 2, 1, 31);
      }
      break;
    default:
      break;
    }
  }
}

ChannelInstrument shifted(const ChannelInstrument &base, Tweak tweak,
                          int shift) {
  auto instrument = base;
  switch (tweak) {
  case Tweak::Volume:
    shift_volume(instrument, shift);
    break;
  case Tweak::Brightness:
    shift_brightness(instrument, shift);
    break;
  default:
    shift_envelopes(instrument, tweak, shift);
    break;
  }
  return instrument;
}

} // namespace

std::optional<int> tweak_value(const ChannelInstrument &instrument,
                               Tweak tweak) {
  if (tweak == Tweak::Brightness) {
    const auto *modulator = strongest_modulator(instrument);
    if (modulator == nullptr) {
      return std::nullopt;
    }
    return 127 - modulator->total_level;
  }

  if (loudest_carrier(instrument) == nullptr) {
    return std::nullopt;
  }
  switch (tweak) {
  case Tweak::Volume:
    return 127 - loudest_carrier(instrument)->total_level;
  case Tweak::Attack:
    return fastest_carrier(instrument)->attack_rate;
  case Tweak::Decay: {
    const auto *op = held_carrier(instrument);
    return op->decay_rate + op->sustain_level * 2;
  }
  case Tweak::Release:
    return longest_release(instrument)->release_rate;
  default:
    return std::nullopt;
  }
}

TweakRange tweak_range(const ChannelInstrument &instrument, Tweak tweak) {
  switch (tweak) {
  case Tweak::Attack:
    return {std::min(kMinAttack, tweak_value(instrument, tweak).value_or(31)),
            31};
  case Tweak::Decay:
    return {0, kMaxDecay};
  case Tweak::Release:
    return {0, 15};
  default:
    return {0, 127};
  }
}

ChannelInstrument with_tweak(const ChannelInstrument &base, Tweak tweak,
                             int value) {
  if (!tweak_value(base, tweak)) {
    return base;
  }
  auto best = base;
  int best_distance = std::abs(*tweak_value(base, tweak) - value);
  // Nearest shift first: of the shifts that land equally close, the smallest
  // leaves the most of the instrument as it was.
  for (int amount = 1; amount <= kMaxShift && best_distance > 0; ++amount) {
    for (const int shift : {amount, -amount}) {
      auto candidate = shifted(base, tweak, shift);
      const int distance = std::abs(*tweak_value(candidate, tweak) - value);
      if (distance < best_distance) {
        best = candidate;
        best_distance = distance;
      }
    }
  }
  return best;
}

std::array<TweakFields, 4> tweak_fields(const ChannelInstrument &instrument,
                                        Tweak tweak) {
  std::array<TweakFields, 4> fields{};
  if (!tweak_value(instrument, tweak)) {
    return fields;
  }
  for (std::size_t i = 0; i < 4; ++i) {
    const auto &op = instrument.operators[i];
    if (!op.enable) {
      continue;
    }
    auto &field = fields[i];
    switch (tweak) {
    case Tweak::Volume:
      field.total_level = is_carrier(instrument, i);
      break;
    case Tweak::Brightness:
      field.total_level = !is_carrier(instrument, i);
      break;
    case Tweak::Attack:
      field.attack_rate = true;
      break;
    case Tweak::Decay:
      field.decay_rate = true;
      field.sustain_level = true;
      break;
    case Tweak::Release:
      field.release_rate = true;
      field.sustain_rate = op.sustain_rate != 0;
      break;
    }
  }
  return fields;
}

std::optional<TweakShape> tweak_shape(const ChannelInstrument &instrument) {
  const auto *loudest = loudest_carrier(instrument);
  if (loudest == nullptr) {
    return std::nullopt;
  }
  const auto *fastest = fastest_carrier(instrument);
  const auto *held = held_carrier(instrument);
  const auto *released = longest_release(instrument);
  const auto fraction = [](int value, int top) {
    return static_cast<float>(value) / static_cast<float>(top);
  };
  TweakShape shape;
  shape.level = 1.0f - fraction(loudest->total_level, kSilentTotalLevel);
  shape.attack = 1.0f - fraction(fastest->attack_rate, 31);
  // A decay rate of 0 never leaves the peak.
  shape.decay =
      held->decay_rate == 0 ? 1.0f : 1.0f - fraction(held->decay_rate, 31);
  shape.sustain =
      held->decay_rate == 0 ? 1.0f : 1.0f - fraction(held->sustain_level, 15);
  shape.hold = fraction(held->sustain_rate, 31);
  shape.release = 1.0f - fraction(released->release_rate, 15);
  return shape;
}

std::string tweak_text(const ChannelInstrument &instrument, Tweak tweak) {
  const auto value = tweak_value(instrument, tweak);
  if (!value) {
    return "-";
  }
  char text[32];
  switch (tweak) {
  case Tweak::Volume:
    std::snprintf(text, sizeof(text), "%.1f dB",
                  (*value - 127) * kDecibelsPerTotalLevel);
    return text;
  case Tweak::Decay: {
    const auto *op = held_carrier(instrument);
    std::snprintf(text, sizeof(text), "DR %d  SL %d", op->decay_rate,
                  op->sustain_level);
    return text;
  }
  case Tweak::Release: {
    const auto *op = longest_release(instrument);
    std::snprintf(text, sizeof(text), "RR %d  SR %d", op->release_rate,
                  op->sustain_rate);
    return text;
  }
  default:
    return std::to_string(*value);
  }
}

} // namespace patch_lab
