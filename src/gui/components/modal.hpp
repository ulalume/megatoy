#pragma once

#include <imgui.h>

namespace ui {

/// What closes a modal besides its own buttons.
enum class ModalDismiss {
  /// Nothing does. For a question that has to be answered, and for progress,
  /// whose Cancel does real work that quietly hiding the dialog would skip.
  None,
  /// Escape, standing in for the modal's own cancel. Clicks outside are left
  /// alone so a stray one cannot throw away a half-typed name.
  Escape,
  /// Escape or a click outside. For a modal that only shows things, where
  /// closing it costs nothing.
  EscapeOrOutsideClick,
};

/// Auto-sized to its contents; what almost every dialog wants.
inline constexpr ImGuiWindowFlags kModalAutoResize =
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_AlwaysAutoResize;
/// For a modal that sets its own size because its contents scroll.
inline constexpr ImGuiWindowFlags kModalFixedSize =
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;

struct ModalScope {
  /// Draw the contents, then call end_modal().
  bool visible = false;
  /// It closed this frame without a button being pressed. Whatever the cancel
  /// button would have cleaned up has to be cleaned up here too.
  bool dismissed = false;
};

/**
 * Open a modal that centres itself, stays centred, and says how it closed.
 *
 * ImGui never closes a modal popup on its own -- NavUpdateCancelRequest
 * skips anything flagged Modal -- so before this every dialog could only be
 * closed by its own buttons, and every way out added here is one this has to
 * report: a dialog that vanishes while its pending callback is still armed
 * is worse than one that cannot be dismissed at all.
 */
ModalScope begin_modal(const char *title, ModalDismiss dismiss,
                       ImGuiWindowFlags flags = kModalAutoResize);

/// Only when the scope was visible.
void end_modal();

} // namespace ui
