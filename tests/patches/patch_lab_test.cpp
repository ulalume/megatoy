// Morph interpolates detune by pitch. The register encoding is sign-magnitude
// (0..7 = 0, +1, +2, +3, 0, -1, -2, -3), so blending the raw values would
// wander through unrelated pitches on the way.

#include "patches/patch_lab.hpp"
#include "ym2612/patch.hpp"

#include "../test_check.hpp"
#include <iostream>

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

} // namespace

int main() {
  test_plus_one_to_minus_two_passes_through_the_pitches_between();
  test_plus_three_to_minus_one_steps_down_one_at_a_time();
  test_zero_is_written_as_register_zero();
  test_every_pair_moves_monotonically_between_its_endpoints();

  std::cout << "All patch lab tests passed\n";
  return 0;
}
