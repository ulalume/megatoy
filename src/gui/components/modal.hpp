#pragma once

#include <imgui.h>

#include <initializer_list>

namespace ui {

/// What closes a modal besides its own buttons.
enum class ModalDismiss {
  /// Nothing does. For a question that has to be answered.
  None,
  /// Escape, standing in for the modal's own cancel.
  Escape,
  /// Escape or a click outside. For a modal that only shows things.
  EscapeOrOutsideClick,
};

/// How wide a dialog is unless it says otherwise. Stated rather than fitted:
/// right-aligning buttons measures the width it would otherwise define.
inline constexpr float kDialogWidth = 400.0f;
/// Passed as the height to fit the contents.
inline constexpr float kDialogAutoHeight = -1.0f;

/// What a dialog button gets when its label does not need more.
inline constexpr float kDialogButtonWidth = 120.0f;

/// The width ImGui will give a button with this label.
float button_width(const char *label);

/// Push the cursor right so a row of buttons of these widths ends flush with
/// the right edge, in the order they are then drawn.
void align_buttons_right(std::initializer_list<float> widths);

struct ModalScope {
  /// Draw the contents, then call end_modal().
  bool visible = false;
  /// Closed this frame without a button. Clean up whatever Cancel cleans up.
  bool dismissed = false;
};

/// Open a modal that centres itself and says how it closed. ImGui never
/// dismisses modals itself, so every way out here is ours to report.
ModalScope begin_modal(const char *title, ModalDismiss dismiss,
                       float width = kDialogWidth,
                       float height = kDialogAutoHeight);

/// Only when the scope was visible.
void end_modal();

} // namespace ui
