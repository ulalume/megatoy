#include "../test_check.hpp"
#include "formats/ym2612_format_adapter.hpp"
#include "ym2612/operator_edit.hpp"
#include "ym2612/operator_text.hpp"
#include "ym2612/patch.hpp" // OperatorSettings comparison

#include <iostream>
#include <string>
#include <vector>

namespace {

using ym2612::OperatorSettings;

OperatorSettings make_operator(int ar, int dr, int sr, int rr, int sl, int tl,
                               int ks, int ml, int dt) {
  OperatorSettings op;
  op.attack_rate = static_cast<uint8_t>(ar);
  op.decay_rate = static_cast<uint8_t>(dr);
  op.sustain_rate = static_cast<uint8_t>(sr);
  op.release_rate = static_cast<uint8_t>(rr);
  op.sustain_level = static_cast<uint8_t>(sl);
  op.total_level = static_cast<uint8_t>(tl);
  op.key_scale = static_cast<uint8_t>(ks);
  op.multiple = static_cast<uint8_t>(ml);
  op.detune = static_cast<uint8_t>(dt);
  return op;
}

void test_a_line_matches_the_ctrmml_layout() {
  const auto op = make_operator(31, 12, 0, 7, 2, 28, 0, 1, 3);
  CHECK(ym2612::format_operator_line(op, 0) ==
        "   31  12   0   7   2  28   0   1   3   0 ; OP1\n");
}

void test_the_ssg_column_packs_type_ssg_and_am() {
  auto op = make_operator(1, 2, 3, 4, 5, 6, 1, 2, 7);
  op.ssg_type_envelope_control = 5;
  op.ssg_enable = true;
  CHECK(ym2612::format_operator_line(op, 1) ==
        "    1   2   3   4   5   6   1   2   7  13 ; OP2\n");
  op.amplitude_modulation_enable = true;
  CHECK(ym2612::format_operator_line(op, 1) ==
        "    1   2   3   4   5   6   1   2   7 113 ; OP2\n");
}

void test_edge_values_round_trip() {
  std::vector<OperatorSettings> ops;

  auto am_only = make_operator(0, 0, 0, 0, 0, 0, 0, 0, 0);
  am_only.amplitude_modulation_enable = true;
  ops.push_back(am_only);

  auto ssg_type5 = make_operator(10, 5, 1, 3, 2, 40, 2, 8, 4);
  ssg_type5.ssg_enable = true;
  ssg_type5.ssg_type_envelope_control = 5;
  ops.push_back(ssg_type5);

  auto both = make_operator(20, 20, 20, 10, 10, 64, 1, 3, 2);
  both.ssg_enable = true;
  both.ssg_type_envelope_control = 7;
  both.amplitude_modulation_enable = true;
  ops.push_back(both);

  auto max = make_operator(31, 31, 31, 15, 15, 127, 3, 15, 7);
  ops.push_back(max);

  // A type with SSG-EG off still round-trips: the type is its own bits.
  auto type_without_ssg = make_operator(5, 5, 5, 5, 5, 5, 0, 1, 0);
  type_without_ssg.ssg_type_envelope_control = 3;
  ops.push_back(type_without_ssg);

  std::string text;
  for (std::size_t i = 0; i < ops.size(); ++i) {
    text += ym2612::format_operator_line(ops[i], static_cast<int>(i % 4));
  }
  const auto parsed = ym2612::parse_operator_lines(text);
  CHECK(parsed.size() == ops.size());
  for (std::size_t i = 0; i < ops.size(); ++i) {
    CHECK(parsed[i] == ops[i]);
  }
}

void test_lines_follow_the_given_display_slots() {
  ym2612::ChannelInstrument instrument;
  for (int slot = 0; slot < 4; ++slot) {
    ym2612::operator_at(instrument, slot).total_level =
        static_cast<uint8_t>(10 * (slot + 1));
  }
  const auto text = ym2612::format_operator_lines(instrument, {1, 3});
  CHECK(text == "    0   0   0   0   0  20   0   0   0   0 ; OP2\n"
                "    0   0   0   0   0  40   0   0   0   0 ; OP4\n");
}

void test_the_three_written_forms_parse() {
  const auto spaced = ym2612::parse_operator_lines(
      "  31  12   0   7   2  28   0   1   3   0 ; OP1");
  CHECK(spaced.size() == 1);
  CHECK(spaced[0] == make_operator(31, 12, 0, 7, 2, 28, 0, 1, 3));

  const auto bare_comment =
      ym2612::parse_operator_lines("1  10   0   5   2  26   0   1   3   0 ;");
  CHECK(bare_comment.size() == 1);
  CHECK(bare_comment[0] == make_operator(1, 10, 0, 5, 2, 26, 0, 1, 3));

  const auto commas =
      ym2612::parse_operator_lines("31,0,10,7,0,24,0,5,7,0");
  CHECK(commas.size() == 1);
  CHECK(commas[0] == make_operator(31, 0, 10, 7, 0, 24, 0, 5, 7));

  const auto mixed =
      ym2612::parse_operator_lines("\t31, 0, 10, 7 ,0,24 0 5 7 108\r\n");
  CHECK(mixed.size() == 1);
  CHECK(mixed[0].amplitude_modulation_enable);
  CHECK(mixed[0].ssg_enable);
  CHECK(mixed[0].ssg_type_envelope_control == 0);
}

void test_a_whole_ctrmml_instrument_yields_four_rows() {
  ym2612::Patch patch;
  patch.name = "Bell";
  patch.instrument.algorithm = 4;
  patch.instrument.feedback = 5;
  for (int slot = 0; slot < 4; ++slot) {
    auto &op = ym2612::operator_at(patch.instrument, slot);
    op = make_operator(31 - slot, slot, 2, 7, 3, 20 + slot, slot % 4, slot + 1,
                       slot + 3);
    op.amplitude_modulation_enable = slot == 2;
    op.ssg_enable = slot == 3;
    op.ssg_type_envelope_control = static_cast<uint8_t>(slot);
  }

  const auto text =
      formats::adapter::serialize_text(ym2612_format::Format::Mml, patch);
  CHECK(text.has_value());

  // CRLF, as a Windows editor would hand it over.
  std::string crlf;
  for (char c : *text) {
    if (c == '\n') {
      crlf += '\r';
    }
    crlf += c;
  }

  for (const std::string &block : {*text, crlf}) {
    const auto rows = ym2612::parse_operator_lines(block);
    CHECK(rows.size() == 4);
    for (int slot = 0; slot < 4; ++slot) {
      auto expected = ym2612::operator_at(patch.instrument, slot);
      expected.enable = OperatorSettings{}.enable;
      CHECK(rows[static_cast<std::size_t>(slot)] == expected);
    }
  }
}

void test_out_of_range_values_clamp() {
  const auto rows = ym2612::parse_operator_lines(
      "99 99 99 99 99 999 9 99 9 0\n"
      "-1 -1 -1 -1 -1 -1 -1 -1 -1 0\n");
  CHECK(rows.size() == 2);
  CHECK(rows[0] == make_operator(31, 31, 31, 15, 15, 127, 3, 15, 7));
  CHECK(rows[1] == make_operator(0, 0, 0, 0, 0, 0, 0, 0, 0));
}

void test_junk_yields_nothing() {
  CHECK(ym2612::parse_operator_lines("").empty());
  CHECK(ym2612::parse_operator_lines("hello world").empty());
  CHECK(ym2612::parse_operator_lines("@1 fm ; Piano\n   4   5\n").empty());
  CHECK(ym2612::parse_operator_lines("1 2 3 4 5 6 7 8 9").empty());
  CHECK(ym2612::parse_operator_lines("1 2 3 4 5 6 7 8 9 10 11").empty());
  CHECK(ym2612::parse_operator_lines("1 2 3 4 5 6 7 8 9 x").empty());
  CHECK(ym2612::parse_operator_lines("A 1 2 3 4 5 6 7 8 9").empty());
  CHECK(ym2612::parse_operator_lines(
            "; 31 12 0 7 2 28 0 1 3 0 commented out")
            .empty());
}

std::vector<int> slots_of(const std::vector<ym2612::OperatorPasteTarget> &t) {
  std::vector<int> slots;
  for (const auto &target : t) {
    slots.push_back(target.slot);
  }
  return slots;
}

std::vector<std::size_t>
rows_of(const std::vector<ym2612::OperatorPasteTarget> &t) {
  std::vector<std::size_t> rows;
  for (const auto &target : t) {
    rows.push_back(target.row);
  }
  return rows;
}

void test_one_row_fills_every_selected_slot() {
  const auto targets = ym2612::operator_paste_targets(1, {0, 2, 3});
  CHECK((slots_of(targets) == std::vector<int>{0, 2, 3}));
  CHECK((rows_of(targets) == std::vector<std::size_t>{0, 0, 0}));
  CHECK(ym2612::operator_paste_targets(1, {}).empty());
}

void test_four_rows_fill_the_instrument_with_at_most_one_selected() {
  for (const auto &selected :
       {std::vector<int>{}, std::vector<int>{2}}) {
    const auto targets = ym2612::operator_paste_targets(4, selected);
    CHECK((slots_of(targets) == std::vector<int>{0, 1, 2, 3}));
    CHECK((rows_of(targets) == std::vector<std::size_t>{0, 1, 2, 3}));
  }
}

void test_rows_fill_the_selection_in_order() {
  const auto four_into_two = ym2612::operator_paste_targets(4, {1, 3});
  CHECK((slots_of(four_into_two) == std::vector<int>{1, 3}));
  CHECK((rows_of(four_into_two) == std::vector<std::size_t>{0, 1}));

  const auto two_into_three = ym2612::operator_paste_targets(2, {0, 1, 2});
  CHECK((slots_of(two_into_three) == std::vector<int>{0, 1}));
  CHECK((rows_of(two_into_three) == std::vector<std::size_t>{0, 1}));

  CHECK(ym2612::operator_paste_targets(2, {}).empty());
  CHECK(ym2612::operator_paste_targets(0, {0, 1}).empty());
}

} // namespace

int main() {
  test_a_line_matches_the_ctrmml_layout();
  test_the_ssg_column_packs_type_ssg_and_am();
  test_edge_values_round_trip();
  test_lines_follow_the_given_display_slots();
  test_the_three_written_forms_parse();
  test_a_whole_ctrmml_instrument_yields_four_rows();
  test_out_of_range_values_clamp();
  test_junk_yields_nothing();
  test_one_row_fills_every_selected_slot();
  test_four_rows_fill_the_instrument_with_at_most_one_selected();
  test_rows_fill_the_selection_in_order();

  std::cout << "All operator text tests passed\n";
  return 0;
}
