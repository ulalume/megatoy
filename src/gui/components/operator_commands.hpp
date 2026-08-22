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
  /**
   * Survives a patch switch on purpose: carrying an operator from one patch
   * into another is most of what copy is for. The selection does not -- see
   * selection_patch_path.
   */
  ym2612::OperatorClipboard clipboard;
  /// The patch the selection was made in. Loading another one drops it.
  std::string selection_patch_path;
  /**
   * The four operator frames are drawn to one height so their borders line
   * up. Measured while drawing and applied on the next frame; measuring and
   * applying in the same one would need a second layout pass.
   */
  float frame_height = 0.0f;
  float pending_frame_height = 0.0f;
  /**
   * Set for the frame an operator acts on a mouse click, so the patch
   * editor's "clicked on nothing, drop the selection" rule can tell a click
   * that landed on an operator's own background from one that missed
   * everything. Both look identical to IsAnyItemHovered().
   */
  bool click_claimed = false;
};

struct OperatorCommandContext {
  ym2612::ChannelInstrument &instrument;
  OperatorEditState &state;
  /// Opens one undo step around the command, with an empty merge key so two
  /// pastes in a row stay two steps.
  std::function<void(const std::string &label)> begin_history;
  std::function<void()> commit_history;
};

bool can_copy_operator(const OperatorEditState &state);
bool can_paste_operator(const OperatorEditState &state);
bool can_swap_operators(const OperatorEditState &state);

/// Labels naming what the command would act on: "Copy OP2", "Paste to OP1,
/// OP3", "Swap OP1 and OP3". The Edit menu already spells out its undo
/// entries this way.
std::string copy_operator_label(const OperatorEditState &state);
std::string paste_operator_label(const OperatorEditState &state);
std::string swap_operators_label(const OperatorEditState &state);

/// Each posts a toast saying what it did, or why it could not. Reachable by
/// shortcut as well as by menu, so refusing has to be visible rather than
/// silent the way a greyed-out menu item is.
void copy_operator_command(OperatorCommandContext &context);
void paste_operator_command(OperatorCommandContext &context);
void swap_operators_command(OperatorCommandContext &context);

/// The three items, shared by the Edit menu and an operator's context menu.
void render_operator_command_items(OperatorCommandContext &context);

} // namespace ui
