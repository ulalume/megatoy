#include "../test_check.hpp"
#include "ym2612/operator_edit.hpp"
#include "ym2612/patch.hpp" // ChannelInstrument comparison

#include <iostream>

namespace {

using ym2612::MultiEditMode;
using ym2612::OperatorEditBaseline;
using ym2612::OperatorField;

constexpr uint8_t slots(std::initializer_list<int> list) {
  uint8_t mask = 0;
  for (int slot : list) {
    mask = static_cast<uint8_t>(mask | (1u << slot));
  }
  return mask;
}

ym2612::ChannelInstrument make_instrument() {
  ym2612::ChannelInstrument instrument;
  for (int slot = 0; slot < 4; ++slot) {
    auto &op = ym2612::operator_at(instrument, slot);
    op.total_level = static_cast<uint8_t>(10 * (slot + 1));
    op.attack_rate = static_cast<uint8_t>(slot + 1);
    op.multiple = static_cast<uint8_t>(slot);
    op.detune = 3; // linear 6, i.e. +3
    op.ssg_type_envelope_control = static_cast<uint8_t>(slot);
    op.enable = true;
  }
  return instrument;
}

int total_level(const ym2612::ChannelInstrument &instrument, int slot) {
  return ym2612::operator_at(instrument, slot).total_level;
}

// Editing OP1 writes register 0 and editing OP2 writes register 2. Getting
// this backwards is the whole reason slots exist.
void test_slot_access_follows_the_display_order() {
  ym2612::ChannelInstrument instrument;
  ym2612::operator_at(instrument, 1).total_level = 42;
  CHECK(instrument.operators[2].total_level == 42);
  CHECK(instrument.operators[1].total_level == 0);
}

void test_detune_reads_and_writes_the_linear_value() {
  ym2612::OperatorSettings op;
  op.detune = 7; // register for -3
  CHECK(ym2612::read_operator_field(op, OperatorField::Detune) == 0);

  ym2612::write_operator_field(op, OperatorField::Detune, 6); // +3
  CHECK(op.detune == 3);
  CHECK(ym2612::read_operator_field(op, OperatorField::Detune) == 6);
}

void test_writes_clamp_to_the_field_range() {
  ym2612::OperatorSettings op;
  ym2612::write_operator_field(op, OperatorField::TotalLevel, 500);
  CHECK(op.total_level == 127);
  ym2612::write_operator_field(op, OperatorField::TotalLevel, -20);
  CHECK(op.total_level == 0);
  ym2612::write_operator_field(op, OperatorField::KeyScale, 9);
  CHECK(op.key_scale == 3);
}

void test_a_relative_edit_moves_the_whole_selection_by_the_delta() {
  auto instrument = make_instrument();
  OperatorEditBaseline baseline;
  ym2612::capture_operator_baseline(baseline, instrument,
                                    OperatorField::TotalLevel, 0);

  // OP1 10 -> 20 carries OP2 20 -> 30.
  ym2612::apply_operator_field_edit(instrument, slots({0, 1}), baseline,
                                    OperatorField::TotalLevel, 20,
                                    MultiEditMode::Relative);
  CHECK(total_level(instrument, 0) == 20);
  CHECK(total_level(instrument, 1) == 30);
  // Unselected operators do not move.
  CHECK(total_level(instrument, 2) == 30);
  CHECK(total_level(instrument, 3) == 40);
}

void test_an_absolute_edit_lands_every_selected_operator_on_one_value() {
  auto instrument = make_instrument();
  OperatorEditBaseline baseline;
  ym2612::capture_operator_baseline(baseline, instrument,
                                    OperatorField::TotalLevel, 0);

  ym2612::apply_operator_field_edit(instrument, slots({0, 1, 3}), baseline,
                                    OperatorField::TotalLevel, 55,
                                    MultiEditMode::Absolute);
  CHECK(total_level(instrument, 0) == 55);
  CHECK(total_level(instrument, 1) == 55);
  CHECK(total_level(instrument, 3) == 55);
  CHECK(total_level(instrument, 2) == 30);
}

// The reason edits anchor to a baseline instead of accumulating: drag an
// operator into its ceiling, drag back, and it has to return to where it
// started rather than following the clamped one down.
void test_a_clamped_operator_returns_to_its_starting_value() {
  ym2612::ChannelInstrument instrument;
  ym2612::operator_at(instrument, 0).total_level = 120;
  ym2612::operator_at(instrument, 1).total_level = 125;

  OperatorEditBaseline baseline;
  ym2612::capture_operator_baseline(baseline, instrument,
                                    OperatorField::TotalLevel, 0);

  const uint8_t selection = slots({0, 1});
  // Push past the ceiling: OP2 clamps at 127.
  ym2612::apply_operator_field_edit(instrument, selection, baseline,
                                    OperatorField::TotalLevel, 127,
                                    MultiEditMode::Relative);
  CHECK(total_level(instrument, 0) == 127);
  CHECK(total_level(instrument, 1) == 127);

  // Same drag, back to where it began.
  ym2612::apply_operator_field_edit(instrument, selection, baseline,
                                    OperatorField::TotalLevel, 120,
                                    MultiEditMode::Relative);
  CHECK(total_level(instrument, 0) == 120);
  CHECK(total_level(instrument, 1) == 125);
}

void test_operators_clamp_independently() {
  ym2612::ChannelInstrument instrument;
  ym2612::operator_at(instrument, 0).total_level = 100;
  ym2612::operator_at(instrument, 1).total_level = 126;

  OperatorEditBaseline baseline;
  ym2612::capture_operator_baseline(baseline, instrument,
                                    OperatorField::TotalLevel, 0);
  ym2612::apply_operator_field_edit(instrument, slots({0, 1}), baseline,
                                    OperatorField::TotalLevel, 110,
                                    MultiEditMode::Relative);

  // OP2 stopping at its ceiling does not hold OP1 back.
  CHECK(total_level(instrument, 0) == 110);
  CHECK(total_level(instrument, 1) == 127);
}

// A relative detune edit has to add its delta to the linear value; adding it
// to the sign-magnitude register value would jump the pitch around.
void test_relative_detune_moves_in_linear_space() {
  ym2612::ChannelInstrument instrument;
  ym2612::operator_at(instrument, 0).detune = 5; // linear 2, i.e. -1
  ym2612::operator_at(instrument, 1).detune = 0; // linear 3, i.e. 0

  OperatorEditBaseline baseline;
  ym2612::capture_operator_baseline(baseline, instrument, OperatorField::Detune,
                                    0);
  // -1 -> +1 is a delta of two linear steps, so 0 becomes +2.
  ym2612::apply_operator_field_edit(instrument, slots({0, 1}), baseline,
                                    OperatorField::Detune, 4,
                                    MultiEditMode::Relative);
  CHECK(ym2612::operator_at(instrument, 0).detune == 1); // +1
  CHECK(ym2612::operator_at(instrument, 1).detune == 2); // +2
}

// The SSG envelope type names a shape, not an amount, so it is copied
// verbatim even when the editor is in relative mode.
void test_the_ssg_type_is_always_absolute() {
  auto instrument = make_instrument();
  CHECK(!ym2612::operator_field_is_relative(OperatorField::SsgType));

  OperatorEditBaseline baseline;
  ym2612::capture_operator_baseline(baseline, instrument, OperatorField::SsgType,
                                    0);
  ym2612::apply_operator_field_edit(instrument, slots({0, 1}), baseline,
                                    OperatorField::SsgType, 6,
                                    MultiEditMode::Relative);
  CHECK(ym2612::operator_at(instrument, 0).ssg_type_envelope_control == 6);
  CHECK(ym2612::operator_at(instrument, 1).ssg_type_envelope_control == 6);
}

void test_a_disabled_operator_still_follows_the_edit() {
  auto instrument = make_instrument();
  ym2612::operator_at(instrument, 1).enable = false;

  OperatorEditBaseline baseline;
  ym2612::capture_operator_baseline(baseline, instrument,
                                    OperatorField::TotalLevel, 0);
  ym2612::apply_operator_field_edit(instrument, slots({0, 1}), baseline,
                                    OperatorField::TotalLevel, 15,
                                    MultiEditMode::Relative);
  CHECK(total_level(instrument, 1) == 25);
  CHECK(!ym2612::operator_at(instrument, 1).enable);
}

void test_an_edit_without_a_baseline_writes_only_the_primary() {
  auto instrument = make_instrument();
  OperatorEditBaseline baseline;
  baseline.primary = 0; // never captured, so active stays false

  ym2612::apply_operator_field_edit(instrument, slots({0, 1}), baseline,
                                    OperatorField::TotalLevel, 99,
                                    MultiEditMode::Relative);
  CHECK(total_level(instrument, 0) == 99);
  CHECK(total_level(instrument, 1) == 20);
}

void test_a_stale_baseline_for_another_field_writes_only_the_primary() {
  auto instrument = make_instrument();
  OperatorEditBaseline baseline;
  ym2612::capture_operator_baseline(baseline, instrument,
                                    OperatorField::AttackRate, 0);

  ym2612::apply_operator_field_edit(instrument, slots({0, 1}), baseline,
                                    OperatorField::TotalLevel, 99,
                                    MultiEditMode::Relative);
  CHECK(total_level(instrument, 0) == 99);
  CHECK(total_level(instrument, 1) == 20);
}

void test_flags_spread_absolutely() {
  auto instrument = make_instrument();
  ym2612::apply_operator_flag_edit(instrument, slots({0, 2}),
                                   &ym2612::OperatorSettings::ssg_enable, true);
  CHECK(ym2612::operator_at(instrument, 0).ssg_enable);
  CHECK(ym2612::operator_at(instrument, 2).ssg_enable);
  CHECK(!ym2612::operator_at(instrument, 1).ssg_enable);
  CHECK(!ym2612::operator_at(instrument, 3).ssg_enable);
}

void test_copying_op1_carries_the_feedback() {
  auto instrument = make_instrument();
  instrument.feedback = 5;

  const auto from_op1 = ym2612::copy_operator(instrument, 0);
  CHECK(from_op1.has_value);
  CHECK(from_op1.feedback.has_value());
  CHECK(*from_op1.feedback == 5);

  const auto from_op2 = ym2612::copy_operator(instrument, 1);
  CHECK(from_op2.has_value);
  CHECK(!from_op2.feedback.has_value());
}

void test_paste_overwrites_the_settings_but_not_the_enable_flag() {
  auto instrument = make_instrument();
  const auto clipboard = ym2612::copy_operator(instrument, 0);
  ym2612::operator_at(instrument, 1).enable = false;

  ym2612::paste_operator(instrument, 1, clipboard);
  CHECK(total_level(instrument, 1) == 10);
  CHECK(ym2612::operator_at(instrument, 1).attack_rate == 1);
  // Muting an operator to listen to another one survives the paste.
  CHECK(!ym2612::operator_at(instrument, 1).enable);
}

void test_pasting_op1_elsewhere_leaves_the_feedback_alone() {
  auto instrument = make_instrument();
  instrument.feedback = 6;
  const auto clipboard = ym2612::copy_operator(instrument, 0);
  instrument.feedback = 2;

  ym2612::paste_operator(instrument, 2, clipboard);
  CHECK(instrument.feedback == 2);

  ym2612::paste_operator(instrument, 0, clipboard);
  CHECK(instrument.feedback == 6);
}

void test_pasting_a_non_op1_copy_onto_op1_keeps_op1s_feedback() {
  auto instrument = make_instrument();
  instrument.feedback = 4;
  const auto clipboard = ym2612::copy_operator(instrument, 1);

  ym2612::paste_operator(instrument, 0, clipboard);
  CHECK(instrument.feedback == 4);
  CHECK(total_level(instrument, 0) == 20);
}

void test_pasting_an_empty_clipboard_changes_nothing() {
  auto instrument = make_instrument();
  ym2612::paste_operator(instrument, 1, ym2612::OperatorClipboard{});
  CHECK(total_level(instrument, 1) == 20);
}

void test_swap_exchanges_settings_but_leaves_the_enable_flags() {
  auto instrument = make_instrument();
  // Three operators muted to audition OP4 on its own: the swap must not
  // undo that.
  ym2612::operator_at(instrument, 0).enable = false;
  ym2612::operator_at(instrument, 3).enable = true;

  ym2612::swap_operators(instrument, 0, 3);
  CHECK(total_level(instrument, 0) == 40);
  CHECK(total_level(instrument, 3) == 10);
  CHECK(!ym2612::operator_at(instrument, 0).enable);
  CHECK(ym2612::operator_at(instrument, 3).enable);
}

void test_swap_does_not_move_the_feedback() {
  auto instrument = make_instrument();
  instrument.feedback = 7;
  ym2612::swap_operators(instrument, 0, 1);
  CHECK(instrument.feedback == 7);
}

void test_swapping_an_operator_with_itself_changes_nothing() {
  auto instrument = make_instrument();
  ym2612::swap_operators(instrument, 2, 2);
  CHECK(total_level(instrument, 2) == 30);
}

void test_swap_round_trips() {
  const auto original = make_instrument();
  auto instrument = original;
  ym2612::swap_operators(instrument, 1, 2);
  ym2612::swap_operators(instrument, 1, 2);
  CHECK(instrument == original);
}

} // namespace

int main() {
  test_slot_access_follows_the_display_order();
  test_detune_reads_and_writes_the_linear_value();
  test_writes_clamp_to_the_field_range();
  test_a_relative_edit_moves_the_whole_selection_by_the_delta();
  test_an_absolute_edit_lands_every_selected_operator_on_one_value();
  test_a_clamped_operator_returns_to_its_starting_value();
  test_operators_clamp_independently();
  test_relative_detune_moves_in_linear_space();
  test_the_ssg_type_is_always_absolute();
  test_a_disabled_operator_still_follows_the_edit();
  test_an_edit_without_a_baseline_writes_only_the_primary();
  test_a_stale_baseline_for_another_field_writes_only_the_primary();
  test_flags_spread_absolutely();
  test_copying_op1_carries_the_feedback();
  test_paste_overwrites_the_settings_but_not_the_enable_flag();
  test_pasting_op1_elsewhere_leaves_the_feedback_alone();
  test_pasting_a_non_op1_copy_onto_op1_keeps_op1s_feedback();
  test_pasting_an_empty_clipboard_changes_nothing();
  test_swap_exchanges_settings_but_leaves_the_enable_flags();
  test_swap_does_not_move_the_feedback();
  test_swapping_an_operator_with_itself_changes_nothing();
  test_swap_round_trips();

  std::cout << "All operator edit tests passed\n";
  return 0;
}
