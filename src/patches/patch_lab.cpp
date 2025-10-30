#include "patch_lab.hpp"

#include "ym2612/types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <string_view>

namespace patch_lab {
namespace {

struct Range {
  int min;
  int max;

  int clamp(int value) const {
    if (value < min) {
      return min;
    }
    if (value > max) {
      return max;
    }
    return value;
  }

  int random(std::mt19937 &rng) const {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
  }

  int random_biased(std::mt19937 &rng, float exponent,
                    bool bias_to_min = true) const {
    if (max <= min) {
      return min;
    }

    std::uniform_real_distribution<double> dist(0.0, 1.0);
    double u = dist(rng);
    double e = std::max(0.001, static_cast<double>(exponent));

    double sample = bias_to_min ? std::pow(u, e) : 1.0 - std::pow(1.0 - u, e);

    int value = static_cast<int>(std::llround(
        static_cast<double>(min) + sample * static_cast<double>(max - min)));
    return clamp(value);
  }
};

struct CategoryDefinition {
  std::string_view id;
  std::string_view display_name;
  std::array<uint8_t, 4> algorithms;
  Range feedback;
  Range mod_total_level;
  Range carrier_total_level;
  Range mod_attack_rate;
  Range carrier_attack_rate;
  Range mod_decay_rate;
  Range carrier_decay_rate;
  Range mod_sustain_rate;
  Range carrier_sustain_rate;
  Range mod_release_rate;
  Range carrier_release_rate;
  Range mod_sustain_level;
  Range carrier_sustain_level;
  Range mod_multiple;
  Range carrier_multiple;
  Range mod_detune;
  Range carrier_detune;
  Range ams;
  Range pms;
  bool enable_lfo;
  Range lfo_frequency;
};

constexpr Range kLfoFrequency{0, 7};
constexpr Range kFeedback{0, 7};
constexpr Range kAlgorithm{0, 7};
constexpr Range kTotalLevel{0, 127};
constexpr Range kAttackRate{0, 31};
constexpr Range kDecayRate{0, 31};
constexpr Range kSustainRate{0, 31};
constexpr Range kReleaseRate{0, 15};
constexpr Range kSustainLevel{0, 15};
constexpr Range kKeyScale{0, 3};
constexpr Range kMultiple{0, 15};
constexpr Range kDetune{0, 7};
constexpr Range kSsgType{0, 7};
constexpr Range kAms{0, 3};
constexpr Range kPms{0, 7};

const std::array<CategoryDefinition, 4> kCategories = {{
    {.id = "bass",
     .display_name = "Low-End Bass",
     .algorithms = {{0, 1, 2, 3}},
     .feedback = Range{4, 7},
     .mod_total_level = Range{40, 96},
     .carrier_total_level = Range{0, 28},
     .mod_attack_rate = Range{24, 31},
     .carrier_attack_rate = Range{18, 28},
     .mod_decay_rate = Range{8, 20},
     .carrier_decay_rate = Range{8, 18},
     .mod_sustain_rate = Range{8, 20},
     .carrier_sustain_rate = Range{4, 12},
     .mod_release_rate = Range{6, 12},
     .carrier_release_rate = Range{3, 8},
     .mod_sustain_level = Range{10, 15},
     .carrier_sustain_level = Range{0, 7},
     .mod_multiple = Range{1, 5},
     .carrier_multiple = Range{0, 3},
     .mod_detune = Range{0, 2},
     .carrier_detune = Range{0, 1},
     .ams = Range{0, 1},
     .pms = Range{0, 1},
     .enable_lfo = false,
     .lfo_frequency = Range{0, 0}},
    {.id = "pad",
     .display_name = "Lush Pad",
     .algorithms = {{3, 4, 5, 5}},
     .feedback = Range{1, 4},
     .mod_total_level = Range{30, 70},
     .carrier_total_level = Range{0, 18},
     .mod_attack_rate = Range{8, 16},
     .carrier_attack_rate = Range{3, 10},
     .mod_decay_rate = Range{4, 12},
     .carrier_decay_rate = Range{4, 10},
     .mod_sustain_rate = Range{2, 8},
     .carrier_sustain_rate = Range{4, 9},
     .mod_release_rate = Range{6, 12},
     .carrier_release_rate = Range{4, 10},
     .mod_sustain_level = Range{4, 12},
     .carrier_sustain_level = Range{1, 4},
     .mod_multiple = Range{0, 4},
     .carrier_multiple = Range{0, 5},
     .mod_detune = Range{0, 2},
     .carrier_detune = Range{0, 2},
     .ams = Range{0, 2},
     .pms = Range{1, 5},
     .enable_lfo = true,
     .lfo_frequency = Range{1, 5}},
    {.id = "bell",
     .display_name = "Chime & Bell",
     .algorithms = {{5, 6, 7, 7}},
     .feedback = Range{0, 2},
     .mod_total_level = Range{45, 110},
     .carrier_total_level = Range{0, 12},
     .mod_attack_rate = Range{26, 31},
     .carrier_attack_rate = Range{26, 31},
     .mod_decay_rate = Range{10, 20},
     .carrier_decay_rate = Range{10, 20},
     .mod_sustain_rate = Range{18, 28},
     .carrier_sustain_rate = Range{18, 28},
     .mod_release_rate = Range{2, 6},
     .carrier_release_rate = Range{8, 15},
     .mod_sustain_level = Range{0, 4},
     .carrier_sustain_level = Range{4, 12},
     .mod_multiple = Range{6, 12},
     .carrier_multiple = Range{0, 7},
     .mod_detune = Range{2, 6},
     .carrier_detune = Range{2, 6},
     .ams = Range{0, 1},
     .pms = Range{0, 2},
     .enable_lfo = false,
     .lfo_frequency = Range{0, 0}},
    {.id = "lead",
     .display_name = "Expressive Lead",
     .algorithms = {{0, 2, 3, 4}},
     .feedback = Range{2, 5},
     .mod_total_level = Range{35, 90},
     .carrier_total_level = Range{0, 16},
     .mod_attack_rate = Range{18, 30},
     .carrier_attack_rate = Range{14, 24},
     .mod_decay_rate = Range{6, 14},
     .carrier_decay_rate = Range{6, 12},
     .mod_sustain_rate = Range{6, 14},
     .carrier_sustain_rate = Range{6, 12},
     .mod_release_rate = Range{6, 12},
     .carrier_release_rate = Range{6, 12},
     .mod_sustain_level = Range{0, 6},
     .carrier_sustain_level = Range{2, 8},
     .mod_multiple = Range{0, 6},
     .carrier_multiple = Range{0, 4},
     .mod_detune = Range{0, 3},
     .carrier_detune = Range{0, 3},
     .ams = Range{0, 3},
     .pms = Range{0, 6},
     .enable_lfo = true,
     .lfo_frequency = Range{0, 6}},
}};

std::vector<CategoryChoice> category_choices() {
  std::vector<CategoryChoice> result;
  result.reserve(kCategories.size());
  for (const auto &category : kCategories) {
    result.push_back(CategoryChoice{std::string(category.id),
                                    std::string(category.display_name)});
  }
  return result;
}

std::mt19937 make_rng(int seed, std::uint32_t &resolved_seed) {
  if (seed < 0) {
    std::random_device rd;
    resolved_seed = rd();
  } else {
    resolved_seed = static_cast<std::uint32_t>(seed);
  }
  return std::mt19937{resolved_seed};
}

bool random_bool(std::mt19937 &rng, double probability = 0.5) {
  std::bernoulli_distribution dist(probability);
  return dist(rng);
}

template <typename T> T lerp_value(T a, T b, float t) {
  return static_cast<T>(
      std::round(static_cast<float>(a) +
                 (static_cast<float>(b) - static_cast<float>(a)) * t));
}

void blend_operator(const ym2612::OperatorSettings &src,
                    ym2612::OperatorSettings &dst,
                    const ym2612::OperatorSettings &other, float t,
                    int operator_index) {
  dst.attack_rate =
      static_cast<uint8_t>(lerp_value(src.attack_rate, other.attack_rate, t));
  dst.decay_rate =
      static_cast<uint8_t>(lerp_value(src.decay_rate, other.decay_rate, t));
  dst.sustain_rate =
      static_cast<uint8_t>(lerp_value(src.sustain_rate, other.sustain_rate, t));
  dst.release_rate =
      static_cast<uint8_t>(lerp_value(src.release_rate, other.release_rate, t));
  dst.sustain_level = static_cast<uint8_t>(
      lerp_value(src.sustain_level, other.sustain_level, t));
  dst.total_level =
      static_cast<uint8_t>(lerp_value(src.total_level, other.total_level, t));
  dst.key_scale =
      static_cast<uint8_t>(lerp_value(src.key_scale, other.key_scale, t));
  dst.multiple =
      static_cast<uint8_t>(lerp_value(src.multiple, other.multiple, t));
  dst.detune = static_cast<uint8_t>(lerp_value(src.detune, other.detune, t));
  dst.ssg_type_envelope_control = src.ssg_type_envelope_control;

  const float threshold = static_cast<float>(operator_index + 1) / 4.0f;
  const bool take_b = t >= threshold;

  if (take_b) {
    dst.ssg_enable = other.ssg_enable;
    dst.ssg_type_envelope_control = other.ssg_type_envelope_control;
    dst.amplitude_modulation_enable = other.amplitude_modulation_enable;
    dst.enable = other.enable;
  } else {
    dst.ssg_enable = src.ssg_enable;
    dst.amplitude_modulation_enable = src.amplitude_modulation_enable;
    dst.enable = src.enable;
  }
}

void clamp_carrier_levels(ym2612::ChannelInstrument &instrument,
                          int max_total_level) {
  auto modulator_count =
      ym2612::algorithm_modulator_count[instrument.algorithm];
  for (int i = modulator_count; i < 4; ++i) {
    auto &op = instrument.operators[i];
    if (op.total_level > max_total_level) {
      op.total_level = static_cast<uint8_t>(max_total_level);
    }
  }
}

void normalize_patch_volume(ym2612::Patch &patch, int max_carrier_level) {
  clamp_carrier_levels(patch.instrument, max_carrier_level);
}

int apply_variation(int value, const Range &range, int amount,
                    std::mt19937 &rng) {
  if (amount <= 0) {
    return range.clamp(value);
  }
  std::uniform_int_distribution<int> delta(-amount, amount);
  return range.clamp(value + delta(rng));
}

void mutate_operator(ym2612::OperatorSettings &op, std::mt19937 &rng,
                     const MutateOptions &options) {
  std::bernoulli_distribution mutate(options.probability);
  auto should_mutate = [&]() { return mutate(rng); };

  if (should_mutate()) {
    int value =
        apply_variation(op.attack_rate, kAttackRate, options.amount, rng);
    op.attack_rate = static_cast<uint8_t>(value);
  }
  if (should_mutate()) {
    int value = apply_variation(op.decay_rate, kDecayRate, options.amount, rng);
    op.decay_rate = static_cast<uint8_t>(value);
  }
  if (should_mutate()) {
    int value =
        apply_variation(op.sustain_rate, kSustainRate, options.amount, rng);
    op.sustain_rate = static_cast<uint8_t>(value);
  }
  if (should_mutate()) {
    int value =
        apply_variation(op.release_rate, kReleaseRate, options.amount, rng);
    op.release_rate = static_cast<uint8_t>(value);
  }
  if (should_mutate()) {
    int value =
        apply_variation(op.sustain_level, kSustainLevel, options.amount, rng);
    op.sustain_level = static_cast<uint8_t>(value);
  }
  if (should_mutate()) {
    int value =
        apply_variation(op.total_level, kTotalLevel, options.amount * 4, rng);
    op.total_level = static_cast<uint8_t>(value);
  }
  if (should_mutate()) {
    int value = apply_variation(op.key_scale, kKeyScale, 1, rng);
    op.key_scale = static_cast<uint8_t>(value);
  }
  if (should_mutate()) {
    int value = apply_variation(op.multiple, kMultiple, options.amount, rng);
    op.multiple = static_cast<uint8_t>(value);
  }
  if (should_mutate()) {
    int value = apply_variation(op.detune, kDetune, 1, rng);
    op.detune = static_cast<uint8_t>(value);
  }

  if (should_mutate()) {
    op.amplitude_modulation_enable = !op.amplitude_modulation_enable;
  }

  const float ssg_probability =
      std::clamp(options.probability * 0.4f, 0.02f, 0.25f);

  if (random_bool(rng, ssg_probability)) {
    if (op.ssg_enable && random_bool(rng, 0.6)) {
      int value =
          apply_variation(op.ssg_type_envelope_control, kSsgType, 1, rng);
      op.ssg_type_envelope_control = static_cast<uint8_t>(value);
    } else {
      op.ssg_enable = !op.ssg_enable;
      if (op.ssg_enable) {
        int value =
            apply_variation(op.ssg_type_envelope_control, kSsgType, 1, rng);
        op.ssg_type_envelope_control = static_cast<uint8_t>(value);
      }
    }
  }
}

ym2612::Patch make_template_patch(const CategoryDefinition &definition,
                                  std::mt19937 &rng) {
  ym2612::Patch patch;
  patch.name = std::string(definition.display_name);
  patch.global = {
      .dac_enable = false,
      .lfo_enable = definition.enable_lfo,
      .lfo_frequency = static_cast<uint8_t>(
          definition.enable_lfo ? definition.lfo_frequency.random(rng)
                                : kLfoFrequency.min),
  };

  patch.channel = {
      .left_speaker = true,
      .right_speaker = true,
      .amplitude_modulation_sensitivity =
          static_cast<uint8_t>(definition.ams.random(rng)),
      .frequency_modulation_sensitivity =
          static_cast<uint8_t>(definition.pms.random(rng)),
  };

  std::uniform_int_distribution<size_t> algorithm_index_dist(
      0, definition.algorithms.size() - 1);
  auto algorithm = definition.algorithms[algorithm_index_dist(rng)];
  patch.instrument.feedback =
      static_cast<uint8_t>(definition.feedback.random(rng));
  patch.instrument.algorithm = algorithm;

  const auto modulator_count =
      ym2612::algorithm_modulator_count[patch.instrument.algorithm];

  for (int i = 0; i < 4; ++i) {
    auto &op = patch.instrument.operators[i];
    const bool is_modulator = i < modulator_count;
    const auto &total_level = is_modulator ? definition.mod_total_level
                                           : definition.carrier_total_level;
    const auto &attack_rate = is_modulator ? definition.mod_attack_rate
                                           : definition.carrier_attack_rate;
    const auto &decay_rate = is_modulator ? definition.mod_decay_rate
                                          : definition.carrier_decay_rate;
    const auto &sustain_rate = is_modulator ? definition.mod_sustain_rate
                                            : definition.carrier_sustain_rate;
    const auto &release_rate = is_modulator ? definition.mod_release_rate
                                            : definition.carrier_release_rate;
    const auto &sustain_level = is_modulator ? definition.mod_sustain_level
                                             : definition.carrier_sustain_level;
    const auto &multiple =
        is_modulator ? definition.mod_multiple : definition.carrier_multiple;
    const auto &detune =
        is_modulator ? definition.mod_detune : definition.carrier_detune;

    op.attack_rate = static_cast<uint8_t>(attack_rate.random(rng));
    op.decay_rate = static_cast<uint8_t>(decay_rate.random(rng));
    op.sustain_rate = static_cast<uint8_t>(sustain_rate.random(rng));
    op.release_rate = static_cast<uint8_t>(release_rate.random(rng));
    op.sustain_level = static_cast<uint8_t>(sustain_level.random(rng));
    const float carrier_exponent = 2.6f;
    const float modulator_exponent = 1.4f;
    int tl = is_modulator
                 ? total_level.random_biased(rng, modulator_exponent, false)
                 : total_level.random_biased(rng, carrier_exponent, true);
    op.total_level = static_cast<uint8_t>(tl);
    op.key_scale = static_cast<uint8_t>(kKeyScale.random(rng));
    op.multiple = static_cast<uint8_t>(multiple.random(rng));
    op.detune = static_cast<uint8_t>(detune.random(rng));
    op.ssg_type_envelope_control = 0;
    op.ssg_enable = false;
    op.amplitude_modulation_enable = random_bool(rng, 0.25);
    op.enable = true;
  }

  normalize_patch_volume(patch, 40);
  return patch;
}

ym2612::Patch make_wild_patch(std::mt19937 &rng) {
  ym2612::Patch patch;
  bool lfo_enable = random_bool(rng, 0.4);
  patch.global = {
      .dac_enable = random_bool(rng),
      .lfo_enable = lfo_enable,
      .lfo_frequency = static_cast<uint8_t>(
          lfo_enable ? kLfoFrequency.random(rng) : kLfoFrequency.min),
  };

  patch.channel = {
      .left_speaker = true,
      .right_speaker = true,
      .amplitude_modulation_sensitivity =
          static_cast<uint8_t>(kAms.random(rng)),
      .frequency_modulation_sensitivity =
          static_cast<uint8_t>(kPms.random(rng)),
  };

  patch.instrument.feedback = static_cast<uint8_t>(kFeedback.random(rng));
  patch.instrument.algorithm = static_cast<uint8_t>(kAlgorithm.random(rng));

  const auto modulator_count =
      ym2612::algorithm_modulator_count[patch.instrument.algorithm];
  for (int i = 0; i < 4; ++i) {
    auto &op = patch.instrument.operators[i];
    op.attack_rate = static_cast<uint8_t>(kAttackRate.random(rng));
    op.decay_rate = static_cast<uint8_t>(kDecayRate.random(rng));
    op.sustain_rate = static_cast<uint8_t>(kSustainRate.random(rng));
    op.release_rate = static_cast<uint8_t>(kReleaseRate.random(rng));
    op.sustain_level = static_cast<uint8_t>(kSustainLevel.random(rng));
    const bool is_modulator = i < modulator_count;
    const Range total_range = is_modulator ? Range{28, 110} : Range{0, 32};
    const float carrier_exponent = 2.8f;
    const float modulator_exponent = 1.3f;
    int tl = is_modulator
                 ? total_range.random_biased(rng, modulator_exponent, false)
                 : total_range.random_biased(rng, carrier_exponent, true);
    op.total_level = static_cast<uint8_t>(tl);
    op.key_scale = static_cast<uint8_t>(kKeyScale.random(rng));
    op.multiple = static_cast<uint8_t>(kMultiple.random(rng));
    op.detune = static_cast<uint8_t>(kDetune.random(rng));
    op.ssg_enable = random_bool(rng, 0.1);
    op.ssg_type_envelope_control =
        op.ssg_enable ? static_cast<uint8_t>(kSsgType.random(rng)) : 0;
    op.amplitude_modulation_enable = random_bool(rng, 0.3);
    op.enable = true;
  }

  normalize_patch_volume(patch, 36);
  return patch;
}

const CategoryDefinition *find_category(std::string_view id) {
  for (const auto &definition : kCategories) {
    if (definition.id == id || definition.display_name == id) {
      return &definition;
    }
  }
  return nullptr;
}

} // namespace

std::vector<CategoryChoice> available_categories() {
  return category_choices();
}

OperationResult merge(const ym2612::Patch &a, const ym2612::Patch &b,
                      const MergeOptions &options) {
  OperationResult result;
  auto rng = make_rng(options.seed, result.seed);

  ym2612::Patch patch = a;
  patch.name =
      a.name.empty() || b.name.empty() ? "Blend" : a.name + " + " + b.name;

  patch.global.dac_enable =
      random_bool(rng) ? a.global.dac_enable : b.global.dac_enable;
  patch.global.lfo_enable =
      random_bool(rng) ? a.global.lfo_enable : b.global.lfo_enable;
  patch.global.lfo_frequency =
      random_bool(rng) ? a.global.lfo_frequency : b.global.lfo_frequency;

  patch.channel.left_speaker =
      random_bool(rng) ? a.channel.left_speaker : b.channel.left_speaker;
  patch.channel.right_speaker =
      random_bool(rng) ? a.channel.right_speaker : b.channel.right_speaker;
  patch.channel.amplitude_modulation_sensitivity =
      random_bool(rng) ? a.channel.amplitude_modulation_sensitivity
                       : b.channel.amplitude_modulation_sensitivity;
  patch.channel.frequency_modulation_sensitivity =
      random_bool(rng) ? a.channel.frequency_modulation_sensitivity
                       : b.channel.frequency_modulation_sensitivity;

  patch.instrument.feedback =
      random_bool(rng) ? a.instrument.feedback : b.instrument.feedback;
  patch.instrument.algorithm =
      random_bool(rng) ? a.instrument.algorithm : b.instrument.algorithm;

  for (int i = 0; i < 4; ++i) {
    patch.instrument.operators[i] = random_bool(rng)
                                        ? a.instrument.operators[i]
                                        : b.instrument.operators[i];
  }

  normalize_patch_volume(patch, 42);

  result.patch = std::move(patch);
  return result;
}

OperationResult random_patch(const RandomOptions &options) {
  OperationResult result;
  auto rng = make_rng(options.seed, result.seed);

  ym2612::Patch patch;
  bool used_template = false;
  if (options.mode == RandomOptions::Mode::Category) {
    const auto *category = find_category(options.category);
    if (category) {
      patch = make_template_patch(*category, rng);
      used_template = true;
    } else {
      patch = make_wild_patch(rng);
    }
  } else {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    double sample = dist(rng);

    const CategoryDefinition *category = nullptr;
    if (sample <= 0.25) {
      category = find_category("bell");
    } else if (sample <= 0.50) {
      category = find_category("pad");
    }

    if (category) {
      patch = make_template_patch(*category, rng);
      used_template = true;
    } else {
      patch = make_wild_patch(rng);
    }
  }

  if (used_template) {
    MutateOptions mutate_options;
    mutate_options.amount = 3;
    mutate_options.probability = 0.6f;
    mutate_options.allow_algorithm_variation = false;
    int iterations = std::max(0, options.mutate_iterations);
    for (int i = 0; i < iterations; ++i) {
      mutate_in_place(patch, mutate_options);
    }
  }

  result.patch = std::move(patch);
  normalize_patch_volume(result.patch, 40);
  return result;
}

OperationResult morph(const ym2612::Patch &a, const ym2612::Patch &b,
                      const MorphOptions &options) {
  OperationResult result;
  result.seed = 0;

  const float mix = std::clamp(options.mix, 0.0f, 1.0f);

  ym2612::Patch patch = a;
  patch.name =
      a.name.empty() || b.name.empty() ? "Morph" : a.name + " <> " + b.name;

  patch.global.dac_enable =
      mix < 0.5f ? a.global.dac_enable : b.global.dac_enable;
  patch.global.lfo_enable =
      mix < 0.5f ? a.global.lfo_enable : b.global.lfo_enable;
  patch.global.lfo_frequency = static_cast<uint8_t>(
      lerp_value(a.global.lfo_frequency, b.global.lfo_frequency, mix));

  patch.channel.left_speaker =
      mix < 0.5f ? a.channel.left_speaker : b.channel.left_speaker;
  patch.channel.right_speaker =
      mix < 0.5f ? a.channel.right_speaker : b.channel.right_speaker;
  patch.channel.amplitude_modulation_sensitivity = static_cast<uint8_t>(
      lerp_value(a.channel.amplitude_modulation_sensitivity,
                 b.channel.amplitude_modulation_sensitivity, mix));
  patch.channel.frequency_modulation_sensitivity = static_cast<uint8_t>(
      lerp_value(a.channel.frequency_modulation_sensitivity,
                 b.channel.frequency_modulation_sensitivity, mix));

  patch.instrument.feedback = static_cast<uint8_t>(
      lerp_value(a.instrument.feedback, b.instrument.feedback, mix));

  if (options.interpolate_algorithm) {
    patch.instrument.algorithm = static_cast<uint8_t>(
        lerp_value(a.instrument.algorithm, b.instrument.algorithm, mix));
  } else {
    patch.instrument.algorithm =
        mix < 0.5f ? a.instrument.algorithm : b.instrument.algorithm;
  }

  for (int i = 0; i < 4; ++i) {
    blend_operator(a.instrument.operators[i], patch.instrument.operators[i],
                   b.instrument.operators[i], mix, i);
  }

  result.patch = std::move(patch);
  normalize_patch_volume(result.patch, 40);
  return result;
}

MutateResult mutate_in_place(ym2612::Patch &patch,
                             const MutateOptions &options) {
  MutateResult result;
  auto rng = make_rng(options.seed, result.seed);

  std::bernoulli_distribution mutate(options.probability);

  if (mutate(rng)) {
    patch.global.lfo_enable = !patch.global.lfo_enable;
  }

  if (patch.global.lfo_enable && mutate(rng)) {
    int value = patch.global.lfo_frequency;
    value = apply_variation(value, kLfoFrequency, 1, rng);
    patch.global.lfo_frequency = static_cast<uint8_t>(value);
  }

  if (mutate(rng)) {
    int value = patch.channel.amplitude_modulation_sensitivity;
    value = apply_variation(value, kAms, 1, rng);
    patch.channel.amplitude_modulation_sensitivity =
        static_cast<uint8_t>(value);
  }

  if (mutate(rng)) {
    int value = patch.channel.frequency_modulation_sensitivity;
    value = apply_variation(value, kPms, options.amount, rng);
    patch.channel.frequency_modulation_sensitivity =
        static_cast<uint8_t>(value);
  }

  if (options.allow_algorithm_variation && mutate(rng)) {
    int value = patch.instrument.algorithm;
    value = apply_variation(value, kAlgorithm, 1, rng);
    patch.instrument.algorithm = static_cast<uint8_t>(value);
  }

  if (mutate(rng)) {
    int value = patch.instrument.feedback;
    value = apply_variation(value, kFeedback, 1, rng);
    patch.instrument.feedback = static_cast<uint8_t>(value);
  }

  for (int i = 0; i < 4; ++i) {
    mutate_operator(patch.instrument.operators[i], rng, options);
  }

  normalize_patch_volume(patch, 42);
  return result;
}

} // namespace patch_lab
