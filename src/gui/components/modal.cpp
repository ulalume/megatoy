#include "modal.hpp"
#include "common.hpp"

#include <algorithm>

namespace ui {
namespace {

bool escape_pressed() {
  // Not while a text field holds the keyboard: there Escape is the field's
  // own "put back what was there", and spending the same key on the whole
  // dialog would cost the user the rest of what they typed. Once the field
  // has let go, a second press closes the dialog.
  return !ImGui::GetIO().WantTextInput &&
         ImGui::IsKeyPressed(ImGuiKey_Escape, false);
}

bool clicked_outside() {
  // A modal dims and swallows everything behind it, so nothing else is left
  // to report the click; the modal has to notice it is not the window under
  // the cursor.
  return ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
         !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                 ImGuiHoveredFlags_AllowWhenBlockedByPopup);
}

bool should_dismiss(ModalDismiss dismiss) {
  if (dismiss == ModalDismiss::None) {
    return false;
  }
  // Stacked modals all get drawn, but only the top one is focused, and only
  // the top one should answer to a key the user meant for it.
  if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    return false;
  }
  if (escape_pressed()) {
    return true;
  }
  return dismiss == ModalDismiss::EscapeOrOutsideClick && clicked_outside();
}

} // namespace

ModalScope begin_modal(const char *title, ModalDismiss dismiss,
                       ImGuiWindowFlags flags) {
  ModalScope scope;

  center_next_window();
  if (!ImGui::BeginPopupModal(title, nullptr, flags)) {
    return scope;
  }

  scope.visible = true;
  // Centring once on appearance is not enough for a modal that resizes after
  // it opens -- About grows when its update check answers -- so it is held.
  force_center_window();

  if (should_dismiss(dismiss)) {
    scope.dismissed = true;
    ImGui::CloseCurrentPopup();
  }
  return scope;
}

void end_modal() { ImGui::EndPopup(); }

float button_width(const char *label) {
  return ImGui::CalcTextSize(label, nullptr, true).x +
         ImGui::GetStyle().FramePadding.x * 2.0f;
}

void align_buttons_right(std::initializer_list<float> widths) {
  if (widths.size() == 0) {
    return;
  }

  float total = 0.0f;
  for (const float width : widths) {
    total += width;
  }
  total += ImGui::GetStyle().ItemSpacing.x *
           static_cast<float>(widths.size() - 1);

  const float offset = ImGui::GetContentRegionAvail().x - total;
  if (offset > 0.0f) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
  }
}

} // namespace ui
