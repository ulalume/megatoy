#include "gui/components/operator_commands.hpp"
#include "core/status.hpp"
#include "ym2612/operator_text.hpp"
#include <imgui.h>
#include <string>
#include <vector>

namespace ui {
namespace {

std::string joined(const OperatorSelection &selection) {
  return operator_selection_label(selection);
}

std::string joined(const std::vector<ym2612::OperatorPasteTarget> &targets) {
  std::string label;
  for (const auto &target : targets) {
    if (!label.empty()) {
      label += ", ";
    }
    label += operator_slot_label(target.slot);
  }
  return label;
}

/// What a paste would do: rows from the system clipboard's text, or none.
struct TextPaste {
  std::vector<ym2612::OperatorSettings> rows;
  std::vector<ym2612::OperatorPasteTarget> targets;
  /// The text is the editor's own copy of a single operator, which the
  /// internal clipboard holds with its feedback.
  bool own_single = false;
};

TextPaste read_text_paste(const OperatorEditState &state,
                          const ym2612::OperatorClipboard &clipboard) {
  TextPaste paste;
  const char *text = ImGui::GetClipboardText();
  if (text == nullptr) {
    return paste;
  }
  paste.rows = ym2612::parse_operator_lines(text);
  paste.targets =
      ym2612::operator_paste_targets(paste.rows.size(), state.selection.slots());
  paste.own_single = clipboard.has_value && paste.rows.size() == 1 &&
                     clipboard.text == text;
  return paste;
}

bool uses_text(const TextPaste &paste) {
  return !paste.rows.empty() && !paste.own_single;
}

bool can_paste(const OperatorEditState &state,
               const ym2612::OperatorClipboard &clipboard,
               const TextPaste &paste) {
  if (uses_text(paste)) {
    return !paste.targets.empty();
  }
  return clipboard.has_value && !state.selection.empty();
}

std::string paste_label(const OperatorEditState &state,
                        const TextPaste &paste) {
  if (uses_text(paste) && !paste.targets.empty()) {
    return "Paste to " + joined(paste.targets);
  }
  return paste_operator_label(state);
}

} // namespace

bool can_copy_operator(const OperatorEditState &state) {
  return state.selection.primary >= 0;
}

bool can_paste_operator(const OperatorEditState &state,
                        const ym2612::OperatorClipboard &clipboard) {
  return can_paste(state, clipboard, read_text_paste(state, clipboard));
}

bool can_swap_operators(const OperatorEditState &state) {
  return state.selection.count() == 2;
}

std::string copy_operator_label(const OperatorEditState &state) {
  if (state.selection.primary < 0) {
    return "Copy Operator";
  }
  return "Copy " + joined(state.selection);
}

std::string paste_operator_label(const OperatorEditState &state) {
  if (state.selection.empty()) {
    return "Paste Operator";
  }
  return "Paste to " + joined(state.selection);
}

std::string swap_operators_label(const OperatorEditState &state) {
  if (state.selection.count() != 2) {
    return "Swap Operators";
  }
  const auto slots = state.selection.slots();
  return "Swap " + operator_slot_label(slots[0]) + " and " +
         operator_slot_label(slots[1]);
}

void copy_operator_command(OperatorCommandContext &context) {
  const int primary = context.state.selection.primary;
  if (primary < 0) {
    megatoy::status::warning("Select an operator to copy.");
    return;
  }

  context.clipboard = ym2612::copy_operator(context.instrument, primary);
  context.clipboard.text = ym2612::format_operator_lines(
      context.instrument, context.state.selection.slots());
  ImGui::SetClipboardText(context.clipboard.text.c_str());
  megatoy::status::info("Copied " + joined(context.state.selection) + ".");
}

void paste_operator_command(OperatorCommandContext &context) {
  auto &state = context.state;
  const TextPaste paste = read_text_paste(state, context.clipboard);

  if (uses_text(paste)) {
    if (paste.targets.empty()) {
      megatoy::status::warning("Select an operator to paste into.");
      return;
    }
    if (context.begin_history) {
      context.begin_history("Paste to " + joined(paste.targets));
    }
    for (const auto &target : paste.targets) {
      ym2612::OperatorClipboard row;
      row.has_value = true;
      row.op = paste.rows[target.row];
      ym2612::paste_operator(context.instrument, target.slot, row);
    }
    if (context.commit_history) {
      context.commit_history();
    }
    if (paste.rows.size() == 1) {
      megatoy::status::success("Pasted into " + joined(paste.targets) + ".");
    } else {
      megatoy::status::success("Pasted " +
                               std::to_string(paste.targets.size()) +
                               " operators into " + joined(paste.targets) +
                               ".");
    }
    return;
  }

  if (!context.clipboard.has_value) {
    megatoy::status::warning("Copy an operator first.");
    return;
  }

  const auto targets = state.selection.slots();
  if (targets.empty()) {
    megatoy::status::warning("Select an operator to paste into.");
    return;
  }

  const std::string label = paste_operator_label(state);
  if (context.begin_history) {
    context.begin_history(label);
  }
  for (int slot : targets) {
    ym2612::paste_operator(context.instrument, slot, context.clipboard);
  }
  if (context.commit_history) {
    context.commit_history();
  }

  megatoy::status::success("Pasted into " + joined(state.selection) + ".");
}

void swap_operators_command(OperatorCommandContext &context) {
  auto &state = context.state;
  if (state.selection.count() != 2) {
    megatoy::status::warning("Select exactly two operators to swap.");
    return;
  }

  const auto slots = state.selection.slots();
  const std::string label = swap_operators_label(state);
  if (context.begin_history) {
    context.begin_history(label);
  }
  ym2612::swap_operators(context.instrument, slots[0], slots[1]);
  if (context.commit_history) {
    context.commit_history();
  }

  megatoy::status::success("Swapped " + operator_slot_label(slots[0]) +
                           " and " + operator_slot_label(slots[1]) + ".");
}

void render_operator_command_items(OperatorCommandContext &context) {
  const auto &state = context.state;
  const bool mac_behavior = ImGui::GetIO().ConfigMacOSXBehaviors;

  if (ImGui::MenuItem(copy_operator_label(state).c_str(),
                      mac_behavior ? "Cmd+C" : "Ctrl+C", false,
                      can_copy_operator(state))) {
    copy_operator_command(context);
  }
  if (!can_copy_operator(state) &&
      ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("Click an operator to select it.");
  }

  const TextPaste paste = read_text_paste(state, context.clipboard);
  const bool paste_enabled = can_paste(state, context.clipboard, paste);
  if (ImGui::MenuItem(paste_label(state, paste).c_str(),
                      mac_behavior ? "Cmd+V" : "Ctrl+V", false,
                      paste_enabled)) {
    paste_operator_command(context);
  }
  if (!paste_enabled &&
      ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip(context.clipboard.has_value || !paste.rows.empty()
                          ? "Select the operators to paste into."
                          : "Copy an operator first.");
  }

  if (ImGui::MenuItem(swap_operators_label(state).c_str(),
                      mac_behavior ? "Cmd+Shift+X" : "Ctrl+Shift+X", false,
                      can_swap_operators(state))) {
    swap_operators_command(context);
  }
  if (!can_swap_operators(state) &&
      ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("Shift-click to select exactly two operators.");
  }
}

} // namespace ui
