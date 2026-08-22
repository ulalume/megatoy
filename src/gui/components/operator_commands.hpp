#pragma once

#include "gui/components/operator_selection.hpp"
#include "ym2612/operator_edit.hpp"
#include <functional>
#include <string>

namespace ui {

/// What the operator editor keeps between frames.
struct OperatorEditState {
  OperatorSelection selection;
  ym2612::OperatorEditBaseline baseline;
  /// Survives a patch switch on purpose; the selection does not.
  ym2612::OperatorClipboard clipboard;
  /// The patch the selection was made in. Loading another one drops it.
  std::string selection_patch_path;
  /// Shared frame height so the four borders line up. Measured while drawing,
  /// applied next frame.
  float frame_height = 0.0f;
  float pending_frame_height = 0.0f;
  /// Set when an operator acts on a click, so "clicked on nothing" can tell
  /// an operator's background from a real miss. Both look alike to ImGui.
  bool click_claimed = false;
};

struct OperatorCommandContext {
  ym2612::ChannelInstrument &instrument;
  OperatorEditState &state;
  /// Opens one undo step; empty merge key, so two pastes stay two steps.
  std::function<void(const std::string &label)> begin_history;
  std::function<void()> commit_history;
};

bool can_copy_operator(const OperatorEditState &state);
bool can_paste_operator(const OperatorEditState &state);
bool can_swap_operators(const OperatorEditState &state);

/// Labels naming the target: "Copy OP2", "Paste to OP1, OP3", "Swap OP1 and
/// OP3", matching how the Edit menu spells out its undo entries.
std::string copy_operator_label(const OperatorEditState &state);
std::string paste_operator_label(const OperatorEditState &state);
std::string swap_operators_label(const OperatorEditState &state);

/// Each posts a toast saying what it did, or why it could not: they are
/// reachable by shortcut, where a greyed-out menu item says nothing.
void copy_operator_command(OperatorCommandContext &context);
void paste_operator_command(OperatorCommandContext &context);
void swap_operators_command(OperatorCommandContext &context);

/// The three items, shared by the Edit menu and an operator's context menu.
void render_operator_command_items(OperatorCommandContext &context);

} // namespace ui
