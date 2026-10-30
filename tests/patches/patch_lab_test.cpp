// Morph interpolates detune by pitch. The register encoding is sign-magnitude
// (0..7 = 0, +1, +2, +3, 0, -1, -2, -3), so blending the raw values would
// wander through unrelated pitches on the way.
//
// Carrier TL is capped after morph, merge and mutate, but a carrier that was
// already a carrier in its source keeps that TL, so a silenced carrier (127)
// stays silent and morph at either end equals the patch it came from.

#include "patches/patch_lab.hpp"
#include "ym2612/patch.hpp"

#include "../test_check.hpp"
#include <algorithm>
#include <iostream>
#include <set>

namespace {

constexpr int kSignedOfRegister[8] = {0, 1, 2, 3, 0, -1, -2, -3};

int signed_detune(uint8_t register_detune) {
  return kSignedOfRegister[register_detune];
}

ym2612::Patch patch_with_detune(uint8_t register_detune) {
  ym2612::Patch patch;
  for (auto &op : patch.instrument.operators) {
    op.detune = register_detune;
  }
  return patch;
}

ym2612::Patch morph_detune(uint8_t a, uint8_t b, float mix) {
  patch_lab::MorphOptions options;
  options.mix = mix;
  return patch_lab::morph(patch_with_detune(a), patch_with_detune(b), options)
      .patch;
}

// All four operators take the same path, so any one of them speaks for all.
uint8_t result_register(uint8_t a, uint8_t b, float mix) {
  const auto patch = morph_detune(a, b, mix);
  for (const auto &op : patch.instrument.operators) {
    CHECK(op.detune == patch.instrument.operators[0].detune);
  }
  return patch.instrument.operators[0].detune;
}

void test_plus_one_to_minus_two_passes_through_the_pitches_between() {
  // +1 -> -2 is 1, 0.25, -0.5, -1.25, -2 on the signed scale. Halves round
  // away from zero, like lerp_value.
  const uint8_t expected[5] = {1, 0, 5, 5, 6};
  const float mix[5] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
  for (int i = 0; i < 5; ++i) {
    CHECK(result_register(1, 6, mix[i]) == expected[i]);
  }
  std::cout << "test_plus_one_to_minus_two_passes_through_the_pitches_between "
               "passed\n";
}

void test_plus_three_to_minus_one_steps_down_one_at_a_time() {
  // +3 -> -1: 3, 2, 1, 0, -1.
  const uint8_t expected[5] = {3, 2, 1, 0, 5};
  const float mix[5] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
  for (int i = 0; i < 5; ++i) {
    CHECK(result_register(3, 5, mix[i]) == expected[i]);
  }
  // -1 -> +3: -1, 0, 1, 2, 3.
  const uint8_t reverse[5] = {5, 0, 1, 2, 3};
  for (int i = 0; i < 5; ++i) {
    CHECK(result_register(5, 3, mix[i]) == reverse[i]);
  }
  std::cout << "test_plus_three_to_minus_one_steps_down_one_at_a_time "
               "passed\n";
}

void test_zero_is_written_as_register_zero() {
  // -1 -> +1 crosses zero at the midpoint.
  CHECK(result_register(5, 1, 0.5f) == 0);
  // Register 4 is the "-0" alias of register 0.
  CHECK(result_register(4, 4, 0.5f) == 0);
  CHECK(result_register(4, 0, 1.0f) == 0);
  std::cout << "test_zero_is_written_as_register_zero passed\n";
}

void test_every_pair_moves_monotonically_between_its_endpoints() {
  constexpr int kSteps = 20;
  for (uint8_t a = 0; a < 8; ++a) {
    for (uint8_t b = 0; b < 8; ++b) {
      const int from = signed_detune(a);
      const int to = signed_detune(b);
      CHECK(signed_detune(result_register(a, b, 0.0f)) == from);
      CHECK(signed_detune(result_register(a, b, 1.0f)) == to);

      int previous = from;
      for (int step = 1; step <= kSteps; ++step) {
        const int current = signed_detune(result_register(
            a, b, static_cast<float>(step) / static_cast<float>(kSteps)));
        if (to >= from) {
          CHECK(current >= previous && current <= to);
        } else {
          CHECK(current <= previous && current >= to);
        }
        previous = current;
      }
    }
  }
  std::cout << "test_every_pair_moves_monotonically_between_its_endpoints "
               "passed\n";
}

ym2612::Patch patch_with_levels(uint8_t algorithm, const int (&levels)[4],
                                int attack_rate_base) {
  ym2612::Patch patch;
  patch.instrument.algorithm = algorithm;
  for (int i = 0; i < 4; ++i) {
    auto &op = patch.instrument.operators[i];
    op.total_level = static_cast<uint8_t>(levels[i]);
    op.attack_rate = static_cast<uint8_t>(attack_rate_base + i);
  }
  return patch;
}

patch_lab::MorphOptions morph_options(float mix, bool interpolate_algorithm) {
  patch_lab::MorphOptions options;
  options.mix = mix;
  options.interpolate_algorithm = interpolate_algorithm;
  return options;
}

void check_same_sound(const ym2612::Patch &actual,
                      const ym2612::Patch &expected) {
  CHECK(actual.global == expected.global);
  CHECK(actual.channel == expected.channel);
  CHECK(actual.instrument == expected.instrument);
}

void test_morph_endpoints_return_the_source_patches() {
  const int silenced[4] = {0, 127, 127, 127};
  const int ordinary[4] = {20, 30, 5, 60};
  const ym2612::Patch sawtooth = patch_with_levels(7, silenced, 1);
  ym2612::Patch four_op = patch_with_levels(4, ordinary, 11);
  four_op.global.lfo_enable = true;
  four_op.global.lfo_frequency = 5;
  four_op.channel.frequency_modulation_sensitivity = 4;
  four_op.instrument.feedback = 6;
  four_op.instrument.operators[1].detune = 4;
  four_op.instrument.operators[2].ssg_enable = true;
  four_op.instrument.operators[3].amplitude_modulation_enable = true;

  for (const bool interpolate_algorithm : {false, true}) {
    const auto at_a =
        patch_lab::morph(sawtooth, four_op,
                         morph_options(0.0f, interpolate_algorithm));
    const auto at_b =
        patch_lab::morph(sawtooth, four_op,
                         morph_options(1.0f, interpolate_algorithm));
    check_same_sound(at_a.patch, sawtooth);
    check_same_sound(at_b.patch, four_op);

    const auto reverse_a = patch_lab::morph(
        four_op, sawtooth, morph_options(0.0f, interpolate_algorithm));
    const auto reverse_b = patch_lab::morph(
        four_op, sawtooth, morph_options(1.0f, interpolate_algorithm));
    check_same_sound(reverse_a.patch, four_op);
    check_same_sound(reverse_b.patch, sawtooth);
  }
  std::cout << "test_morph_endpoints_return_the_source_patches passed\n";
}

void test_morph_keeps_a_silenced_carrier_silent() {
  const int silenced[4] = {0, 127, 127, 127};
  const ym2612::Patch a = patch_with_levels(7, silenced, 1);
  const ym2612::Patch b = patch_with_levels(7, silenced, 11);
  for (const float mix : {0.25f, 0.5f, 0.75f}) {
    const auto patch = patch_lab::morph(a, b, morph_options(mix, false)).patch;
    CHECK(patch.instrument.operators[0].total_level == 0);
    for (int i = 1; i < 4; ++i) {
      CHECK(patch.instrument.operators[i].total_level == 127);
    }
  }
  std::cout << "test_morph_keeps_a_silenced_carrier_silent passed\n";
}

void test_morph_interpolates_a_carrier_between_silent_and_loud() {
  const int silenced[4] = {0, 127, 127, 127};
  const int loud[4] = {0, 0, 0, 0};
  const ym2612::Patch a = patch_with_levels(7, silenced, 1);
  const ym2612::Patch b = patch_with_levels(7, loud, 1);
  const auto patch = patch_lab::morph(a, b, morph_options(0.5f, false)).patch;
  // 127 -> 0 at the midpoint, rounded away from zero.
  CHECK(patch.instrument.operators[1].total_level == 64);
  const auto quarter =
      patch_lab::morph(a, b, morph_options(0.25f, false)).patch;
  CHECK(quarter.instrument.operators[1].total_level == 95);
  std::cout << "test_morph_interpolates_a_carrier_between_silent_and_loud "
               "passed\n";
}

void test_morph_caps_an_operator_that_changes_from_modulator_to_carrier() {
  // Operator 2 modulates in ALG0 (a) and carries in ALG4 (b, the algorithm
  // chosen from the midpoint on). Only its carrier TL in b is exempt from the
  // cap, and that is already below it.
  const int modulating[4] = {10, 10, 80, 0};
  const int carrying[4] = {10, 10, 10, 0};
  const ym2612::Patch a = patch_with_levels(0, modulating, 1);
  const ym2612::Patch b = patch_with_levels(4, carrying, 1);

  const auto midpoint =
      patch_lab::morph(a, b, morph_options(0.5f, false)).patch;
  CHECK(midpoint.instrument.algorithm == 4);
  // 80 -> 10 blends to 45 at the midpoint, above the cap of 40.
  CHECK(midpoint.instrument.operators[2].total_level == 40);

  const auto later = patch_lab::morph(a, b, morph_options(0.75f, false)).patch;
  CHECK(later.instrument.algorithm == 4);
  CHECK(later.instrument.operators[2].total_level == 28);
  std::cout << "test_morph_caps_an_operator_that_changes_from_modulator_to_"
               "carrier passed\n";
}

// The source of a merged operator is found by its attack rate.
void test_merge_keeps_silenced_carriers_and_caps_former_modulators() {
  const int modulating[4] = {90, 91, 92, 93};
  const int silenced[4] = {0, 127, 127, 127};
  const ym2612::Patch a = patch_with_levels(0, modulating, 10);
  const ym2612::Patch b = patch_with_levels(7, silenced, 20);

  bool kept_silenced = false;
  bool capped_former_modulator = false;
  for (int seed = 0; seed < 64; ++seed) {
    patch_lab::MergeOptions options;
    options.seed = seed;
    const auto patch = patch_lab::merge(a, b, options).patch;
    const int algorithm = patch.instrument.algorithm;
    for (int i = 0; i < 4; ++i) {
      const auto &op = patch.instrument.operators[i];
      const bool from_a = op.attack_rate == 10 + i;
      CHECK(from_a || op.attack_rate == 20 + i);
      const auto &source = from_a ? a : b;
      const int source_level = source.instrument.operators[i].total_level;
      const bool carries_in_result =
          i >= ym2612::algorithm_modulator_count[algorithm];
      const bool carried_in_source =
          i >= ym2612::algorithm_modulator_count[source.instrument.algorithm];

      int expected = source_level;
      if (carries_in_result) {
        expected = std::min(
            source_level, carried_in_source ? std::max(42, source_level) : 42);
      }
      CHECK(op.total_level == expected);

      if (carries_in_result && carried_in_source && source_level == 127) {
        kept_silenced = true;
      }
      if (carries_in_result && !carried_in_source && source_level > 42) {
        CHECK(op.total_level == 42);
        capped_former_modulator = true;
      }
    }
  }
  CHECK(kept_silenced);
  CHECK(capped_former_modulator);
  std::cout << "test_merge_keeps_silenced_carriers_and_caps_former_modulators "
               "passed\n";
}

void test_mutate_does_not_pull_a_silenced_carrier_down_to_the_cap() {
  const int silenced[4] = {0, 127, 127, 127};
  for (int seed = 0; seed < 32; ++seed) {
    ym2612::Patch patch = patch_with_levels(7, silenced, 1);
    patch_lab::MutateOptions options;
    options.seed = seed;
    options.probability = 1.0f;
    options.amount = 2;
    options.allow_algorithm_variation = false;
    patch_lab::mutate_in_place(patch, options);
    CHECK(patch.instrument.algorithm == 7);
    // TL varies by up to amount * 4 = 8 and never below the silenced value's
    // neighbourhood.
    for (int i = 1; i < 4; ++i) {
      CHECK(patch.instrument.operators[i].total_level >= 127 - 8);
    }
  }
  std::cout << "test_mutate_does_not_pull_a_silenced_carrier_down_to_the_cap "
               "passed\n";
}

void test_mutate_caps_an_operator_that_becomes_a_carrier() {
  const int modulating[4] = {90, 91, 92, 93};
  bool changed_role = false;
  for (int seed = 0; seed < 64; ++seed) {
    const ym2612::Patch original = patch_with_levels(3, modulating, 1);
    ym2612::Patch patch = original;
    patch_lab::MutateOptions options;
    options.seed = seed;
    options.probability = 1.0f;
    options.amount = 2;
    patch_lab::mutate_in_place(patch, options);

    const int algorithm = patch.instrument.algorithm;
    for (int i = 0; i < 4; ++i) {
      const bool carries_in_result =
          i >= ym2612::algorithm_modulator_count[algorithm];
      const bool carried_before = i >= ym2612::algorithm_modulator_count[3];
      const int level = patch.instrument.operators[i].total_level;
      if (carries_in_result && !carried_before) {
        CHECK(level <= 42);
        changed_role = true;
      }
      if (carries_in_result && carried_before) {
        // Operator 3 was already a carrier at 93 and keeps its own range.
        CHECK(level >= 93 - 8);
      }
    }
  }
  CHECK(changed_role);
  std::cout << "test_mutate_caps_an_operator_that_becomes_a_carrier passed\n";
}

void check_register_ranges(const ym2612::Patch &patch) {
  CHECK(patch.global.lfo_frequency <= 7);
  CHECK(patch.channel.amplitude_modulation_sensitivity <= 3);
  CHECK(patch.channel.frequency_modulation_sensitivity <= 7);
  CHECK(patch.instrument.algorithm <= 7);
  CHECK(patch.instrument.feedback <= 7);
  for (const auto &op : patch.instrument.operators) {
    CHECK(op.attack_rate <= 31);
    CHECK(op.decay_rate <= 31);
    CHECK(op.sustain_rate <= 31);
    CHECK(op.release_rate <= 15);
    CHECK(op.sustain_level <= 15);
    CHECK(op.total_level <= 127);
    CHECK(op.key_scale <= 3);
    CHECK(op.multiple <= 15);
    CHECK(op.detune <= 7);
    CHECK(op.ssg_type_envelope_control <= 7);
  }
}

void test_category_ids_and_labels_generate_the_same_seeded_sound() {
  const auto categories = patch_lab::available_categories();
  CHECK(categories.size() == 4);
  std::set<std::string> ids;
  for (const auto &category : categories) {
    CHECK(!category.label.empty());
    CHECK(ids.insert(category.id).second);
    patch_lab::RandomOptions options;
    options.mode = patch_lab::RandomOptions::Mode::Category;
    options.category = category.id;
    options.seed = 314;
    const auto by_id = patch_lab::random_patch(options);
    options.category = category.label;
    const auto by_label = patch_lab::random_patch(options);
    CHECK(by_id.seed == 314);
    CHECK(by_label.seed == by_id.seed);
    CHECK(by_label.patch == by_id.patch);
    CHECK(by_id.patch.name.empty());
    CHECK(by_id.patch.channel.left_speaker);
    CHECK(by_id.patch.channel.right_speaker);
    CHECK(!by_id.patch.global.dac_enable);
    check_register_ranges(by_id.patch);
    if (category.id == "bass" || category.id == "bell") {
      CHECK(!by_id.patch.global.lfo_enable);
      CHECK(by_id.patch.global.lfo_frequency == 0);
    } else {
      CHECK(by_id.patch.global.lfo_enable);
    }
    options.mutate_iterations = -5;
    CHECK(patch_lab::random_patch(options).patch == by_id.patch);
  }
  CHECK((ids == std::set<std::string>{"bass", "bell", "lead", "pad"}));
}

void test_seeded_random_patches_repeat_and_stay_in_register_ranges() {
  for (const auto mode : {patch_lab::RandomOptions::Mode::Wild,
                          patch_lab::RandomOptions::Mode::Category}) {
    for (const int seed : {0, 1, 2, 3, 42, 1234}) {
      patch_lab::RandomOptions options;
      options.mode = mode;
      options.category = "unknown category";
      options.seed = seed;
      const auto first = patch_lab::random_patch(options);
      const auto again = patch_lab::random_patch(options);
      CHECK(first.seed == static_cast<uint32_t>(seed));
      CHECK(first.patch == again.patch);
      CHECK(first.patch.name.empty());
      check_register_ranges(first.patch);
      const auto &instrument = first.patch.instrument;
      for (int i = 0; i < 4; ++i) {
        CHECK(instrument.operators[i].enable);
        if (i >= ym2612::algorithm_modulator_count[instrument.algorithm]) {
          CHECK(instrument.operators[i].total_level <= 40);
        }
      }
    }
  }
}

void test_seeded_mutation_preserves_name_pan_mutes_and_fixed_algorithm() {
  ym2612::Patch source;
  source.name = "Quiet bass";
  source.channel.left_speaker = false;
  source.channel.right_speaker = true;
  source.instrument.algorithm = 4;
  source.instrument.operators[1].enable = false;
  source.instrument.operators[2].ssg_enable = true;
  for (const int amount : {0, -3, 4}) {
    patch_lab::MutateOptions options;
    options.seed = 15;
    options.amount = amount;
    options.probability = 1.0f;
    options.allow_algorithm_variation = false;
    auto first = source;
    auto again = source;
    CHECK(patch_lab::mutate_in_place(first, options).seed == 15);
    CHECK(patch_lab::mutate_in_place(again, options).seed == 15);
    CHECK(first == again);
    CHECK(first.name == source.name);
    CHECK(first.channel.left_speaker == source.channel.left_speaker);
    CHECK(first.channel.right_speaker == source.channel.right_speaker);
    CHECK(first.global.dac_enable == source.global.dac_enable);
    CHECK(first.instrument.algorithm == source.instrument.algorithm);
    for (int i = 0; i < 4; ++i) {
      CHECK(first.instrument.operators[i].enable ==
            source.instrument.operators[i].enable);
    }
    check_register_ranges(first);
  }
}

void test_morph_clamps_mix_and_switches_flags_at_operator_thresholds() {
  auto a = patch_with_detune(1);
  auto b = patch_with_detune(6);
  a.name = "First";
  b.name = "Second";
  a.instrument.algorithm = 0;
  b.instrument.algorithm = 7;
  b.global.lfo_enable = true;
  b.global.lfo_frequency = 6;
  for (auto &op : b.instrument.operators) {
    op.enable = false;
    op.ssg_enable = true;
    op.ssg_type_envelope_control = 5;
    op.amplitude_modulation_enable = true;
  }
  check_same_sound(patch_lab::morph(a, b, morph_options(-2.0f, true)).patch, a);
  check_same_sound(patch_lab::morph(a, b, morph_options(2.0f, true)).patch, b);
  for (const float mix : {0.24f, 0.25f, 0.49f, 0.5f, 0.74f, 0.75f}) {
    const auto result = patch_lab::morph(a, b, morph_options(mix, false));
    CHECK(result.seed == 0);
    CHECK(result.patch.name.empty());
    CHECK(result.patch.global.lfo_enable == (mix >= 0.5f));
    CHECK(result.patch.instrument.algorithm == (mix < 0.5f ? 0 : 7));
    for (int i = 0; i < 4; ++i) {
      const bool from_b = mix >= static_cast<float>(i + 1) / 4.0f;
      const auto &op = result.patch.instrument.operators[i];
      CHECK(op.enable == !from_b);
      CHECK(op.ssg_enable == from_b);
      CHECK(op.amplitude_modulation_enable == from_b);
      CHECK(op.ssg_type_envelope_control == (from_b ? 5 : 0));
    }
  }
  const auto interpolated = patch_lab::morph(a, b, morph_options(0.5f, true));
  CHECK(interpolated.patch.instrument.algorithm == 4);
  CHECK(interpolated.patch.global.lfo_frequency == 3);
}

void test_merge_repeats_without_modifying_either_source() {
  const int levels[4] = {10, 20, 30, 40};
  auto a = patch_with_levels(7, levels, 1);
  auto b = patch_with_levels(7, levels, 20);
  a.name = "First";
  b.name = "Second";
  a.instrument.feedback = 1;
  b.instrument.feedback = 6;
  const auto original_a = a;
  const auto original_b = b;
  patch_lab::MergeOptions options;
  options.seed = 100;
  const auto result = patch_lab::merge(a, b, options);
  CHECK(result.seed == 100);
  CHECK(result.patch == patch_lab::merge(a, b, options).patch);
  CHECK(result.patch.name.empty());
  CHECK(a == original_a);
  CHECK(b == original_b);
  CHECK(result.patch.instrument.feedback == 1 ||
        result.patch.instrument.feedback == 6);
  for (int i = 0; i < 4; ++i) {
    const auto &op = result.patch.instrument.operators[i];
    CHECK(op == a.instrument.operators[i] || op == b.instrument.operators[i]);
  }
}

} // namespace

int main() {
  test_plus_one_to_minus_two_passes_through_the_pitches_between();
  test_plus_three_to_minus_one_steps_down_one_at_a_time();
  test_zero_is_written_as_register_zero();
  test_every_pair_moves_monotonically_between_its_endpoints();
  test_morph_endpoints_return_the_source_patches();
  test_morph_keeps_a_silenced_carrier_silent();
  test_morph_interpolates_a_carrier_between_silent_and_loud();
  test_morph_caps_an_operator_that_changes_from_modulator_to_carrier();
  test_merge_keeps_silenced_carriers_and_caps_former_modulators();
  test_mutate_does_not_pull_a_silenced_carrier_down_to_the_cap();
  test_mutate_caps_an_operator_that_becomes_a_carrier();
  test_category_ids_and_labels_generate_the_same_seeded_sound();
  test_seeded_random_patches_repeat_and_stay_in_register_ranges();
  test_seeded_mutation_preserves_name_pan_mutes_and_fixed_algorithm();
  test_morph_clamps_mix_and_switches_flags_at_operator_thresholds();
  test_merge_repeats_without_modifying_either_source();

  std::cout << "All patch lab tests passed\n";
  return 0;
}
