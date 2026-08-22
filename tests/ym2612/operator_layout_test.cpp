#include "../test_check.hpp"
#include "formats/ym2612_format_adapter.hpp"
#include "ym2612/types.hpp"

#include <array>
#include <iostream>
#include <set>

// The editor draws operators as OP1..OP4 left to right; the chip stores them
// in another order. Everything that addresses an operator by name depends on
// the two agreeing, so the mapping is pinned here.

namespace {

// The display slot the user sees (0 == "OP1") to the index into
// ChannelInstrument::operators.
int register_index_of_slot(int slot) {
  return static_cast<int>(ym2612::all_operator_indices[slot]);
}

void test_display_slots_map_to_the_documented_register_indices() {
  CHECK(register_index_of_slot(0) == 0); // OP1
  CHECK(register_index_of_slot(1) == 2); // OP2
  CHECK(register_index_of_slot(2) == 1); // OP3
  CHECK(register_index_of_slot(3) == 3); // OP4
}

void test_the_mapping_is_a_bijection() {
  std::set<int> seen;
  for (int slot = 0; slot < 4; ++slot) {
    const int index = register_index_of_slot(slot);
    CHECK(index >= 0 && index < 4);
    CHECK(seen.insert(index).second);
  }
  CHECK(seen.size() == 4);
}

// The modulator/carrier split is positional in register order, which is why
// the editor classifies with the register index and not the display slot.
bool slot_is_carrier(int slot, uint8_t algorithm) {
  return register_index_of_slot(slot) >=
         ym2612::algorithm_modulator_count[algorithm];
}

void test_algorithm_zero_has_one_carrier() {
  // Serial chain: only the last operator in register order reaches the output.
  CHECK(!slot_is_carrier(0, 0)); // OP1, register 0
  CHECK(!slot_is_carrier(2, 0)); // OP3, register 1
  CHECK(!slot_is_carrier(1, 0)); // OP2, register 2
  CHECK(slot_is_carrier(3, 0));  // OP4, register 3
}

void test_algorithm_seven_is_all_carriers() {
  for (int slot = 0; slot < 4; ++slot) {
    CHECK(slot_is_carrier(slot, 7));
  }
}

void test_carrier_count_matches_the_modulator_table() {
  for (uint8_t algorithm = 0; algorithm < 8; ++algorithm) {
    int carriers = 0;
    for (int slot = 0; slot < 4; ++slot) {
      if (slot_is_carrier(slot, algorithm)) {
        ++carriers;
      }
    }
    CHECK(carriers == 4 - ym2612::algorithm_modulator_count[algorithm]);
  }
}

// Velocity scaling attenuates carriers only, and shares total_level with
// multi-operator editing.
void test_velocity_scaling_only_touches_carriers() {
  ym2612::ChannelInstrument instrument;
  instrument.algorithm = 0; // register 3 is the only carrier
  for (auto &op : instrument.operators) {
    op.total_level = 20;
  }

  const auto quiet = instrument.clone_with_velocity(1, 100);
  CHECK(quiet.operators[0].total_level == 20);
  CHECK(quiet.operators[1].total_level == 20);
  CHECK(quiet.operators[2].total_level == 20);
  CHECK(quiet.operators[3].total_level > 20);
}

void test_velocity_scaling_clamps_at_the_register_maximum() {
  ym2612::ChannelInstrument instrument;
  instrument.algorithm = 7; // every operator is a carrier
  for (auto &op : instrument.operators) {
    op.total_level = 120;
  }

  const auto quiet = instrument.clone_with_velocity(0, 100);
  for (const auto &op : quiet.operators) {
    CHECK(op.total_level == 127);
  }
}

// Detune is stored sign-magnitude, so the register value is not monotonic in
// pitch; the slider and any delta work on the linear 0..6 instead.
void test_detune_linear_values_round_trip() {
  for (int linear = 0; linear <= 6; ++linear) {
    const uint8_t register_value =
        formats::adapter::detune_from_linear(linear);
    CHECK(formats::adapter::detune_to_linear(register_value) == linear);
  }
}

void test_detune_linear_order_is_monotonic_in_pitch() {
  // -3 .. +3 in order; the register values behind them are not sorted.
  const std::array<uint8_t, 7> registers = {7, 6, 5, 0, 1, 2, 3};
  for (int linear = 0; linear <= 6; ++linear) {
    CHECK(formats::adapter::detune_from_linear(linear) ==
          registers[static_cast<size_t>(linear)]);
  }
}

void test_detune_negative_zero_alias_normalizes() {
  // Register 4 is an equivalent encoding of "-0"; the editor collapses it
  // onto the canonical 0 as soon as the slider is touched.
  CHECK(formats::adapter::detune_to_linear(4) == 3);
  CHECK(formats::adapter::detune_from_linear(3) == 0);
}

} // namespace

int main() {
  test_display_slots_map_to_the_documented_register_indices();
  test_the_mapping_is_a_bijection();
  test_algorithm_zero_has_one_carrier();
  test_algorithm_seven_is_all_carriers();
  test_carrier_count_matches_the_modulator_table();
  test_velocity_scaling_only_touches_carriers();
  test_velocity_scaling_clamps_at_the_register_maximum();
  test_detune_linear_values_round_trip();
  test_detune_linear_order_is_monotonic_in_pitch();
  test_detune_negative_zero_alias_normalizes();

  std::cout << "All operator layout tests passed\n";
  return 0;
}
