#include "gui/components/operator_commands.hpp"
#include "core/status.hpp"
#include <imgui.h>

namespace ui {
namespace {

std::string joined(const OperatorSelection &selection) {
  return operator_selection_label(selection);
}

} // namespace

bool can_copy_operator(const OperatorEditState &state) {
  return state.selection.primary >= 0;
}

bool can_paste_operator(const OperatorEditState &state) {
  return state.clipboard.has_value && !state.selection.empty();
}

bool can_swap_operators(const OperatorEditState &state) {
  return state.selection.count() == 2;
}

std::string copy_operator_label(const OperatorEditState &state) {
  if (state.selection.primary < 0) {
    return "Copy Operator";
  }
  return "Copy " + operator_slot_label(state.selection.primary);
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

  context.state.clipboard = ym2612::copy_operator(context.instrument, primary);
  const bool with_feedback = context.state.clipboard.feedback.has_value();
  megatoy::status::info("Copied " + operator_slot_label(primary) +
                        (with_feedback ? " and its feedback." : "."));
}

void paste_operator_command(OperatorCommandContext &context) {
  auto &state = context.state;
  if (!state.clipboard.has_value) {
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
    ym2612::paste_operator(context.instrument, slot, state.clipboard);
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

  if (ImGui::MenuItem(paste_operator_label(state).c_str(),
                      mac_behavior ? "Cmd+V" : "Ctrl+V", false,
                      can_paste_operator(state))) {
    paste_operator_command(context);
  }
  if (!can_paste_operator(state) &&
      ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip(state.clipboard.has_value
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
