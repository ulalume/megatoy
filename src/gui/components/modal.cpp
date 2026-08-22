#include "modal.hpp"
#include "common.hpp"

#include <algorithm>

namespace ui {
namespace {

bool escape_pressed() {
  // Not while a text field holds the keyboard: there Escape reverts the
  // field. A second press, once it has let go, closes the dialog.
  return !ImGui::GetIO().WantTextInput &&
         ImGui::IsKeyPressed(ImGuiKey_Escape, false);
}

bool clicked_outside() {
  // The modal swallows everything behind it, so it has to notice for itself
  // that it is not the window under the cursor.
  return ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
         !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                 ImGuiHoveredFlags_AllowWhenBlockedByPopup);
}

bool should_dismiss(ModalDismiss dismiss) {
  if (dismiss == ModalDismiss::None) {
    return false;
  }
  // Stacked modals all draw; only the focused one should answer.
  if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    return false;
  }
  if (escape_pressed()) {
    return true;
  }
  return dismiss == ModalDismiss::EscapeOrOutsideClick && clicked_outside();
}

} // namespace

ModalScope begin_modal(const char *title, ModalDismiss dismiss, float width,
                       float height) {
  ModalScope scope;

  // ImGui clamps neither an explicit size nor auto-resize to the viewport,
  // and buttons past the bottom edge cannot be reached.
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const ImVec2 margin(48.0f, 48.0f);
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(0.0f, 0.0f),
      ImVec2(std::max(viewport->WorkSize.x - margin.x, 240.0f),
             std::max(viewport->WorkSize.y - margin.y, 160.0f)));

  // A non-positive component means fit the contents on that axis.
  ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
  center_next_window();
  if (!ImGui::BeginPopupModal(title, nullptr,
                              ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoResize)) {
    return scope;
  }

  scope.visible = true;
  // Held, not set once: About grows when its update check answers.
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
  total +=
      ImGui::GetStyle().ItemSpacing.x * static_cast<float>(widths.size() - 1);

  const float offset = ImGui::GetContentRegionAvail().x - total;
  if (offset > 0.0f) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
  }
}

} // namespace ui
