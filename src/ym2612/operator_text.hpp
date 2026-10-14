#pragma once

#include "ym2612/types.hpp"
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/**
 * Operators as ctrmml MML text, one row per operator:
 *
 *   AR DR SR RR SL TL KS ML DT SSG
 *
 * DT is the register value. SSG packs the envelope type (0-7), +8 when SSG-EG
 * is on and +100 when AM is on. Display slots as in operator_edit.hpp.
 */
namespace ym2612 {

/// "   31  12   0   7   2  28   0   1   3   0 ; OP1", newline-terminated.
std::string format_operator_line(const OperatorSettings &op, int display_slot);

/// One line per slot, in the order given.
std::string format_operator_lines(const ChannelInstrument &instrument,
                                  const std::vector<int> &display_slots);

/**
 * Every line that holds exactly ten integers, separated by spaces and/or
 * commas, with anything from ';' on ignored. Other lines are skipped.
 * Values are clamped to their fields; `enable` keeps its default.
 */
std::vector<OperatorSettings> parse_operator_lines(std::string_view text);

struct OperatorPasteTarget {
  int slot;
  std::size_t row;
};

/**
 * Where `row_count` parsed rows go, given the selected display slots in
 * ascending order. One row fills every selected slot; four rows with at most
 * one slot selected fill OP1..OP4; otherwise rows fill the selected slots in
 * order, as far as both go.
 */
std::vector<OperatorPasteTarget>
operator_paste_targets(std::size_t row_count,
                       const std::vector<int> &selected_slots);

} // namespace ym2612
