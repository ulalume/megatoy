#include "modal.hpp"
#include "common.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <vector>

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
                       float height, styles::MegatoyCol background) {
  ModalScope scope;

  // ImGui clamps neither an explicit size nor auto-resize to the viewport,
  // and buttons past the bottom edge cannot be reached.
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const ImVec2 margin = ui::scale::px(ImVec2(48.0f, 48.0f));
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(0.0f, 0.0f),
      ImVec2(std::max(viewport->WorkSize.x - margin.x, ui::scale::px(240.0f)),
             std::max(viewport->WorkSize.y - margin.y, ui::scale::px(160.0f))));

  // A non-positive component means fit the contents on that axis, and stays
  // non-positive through the scale.
  ImGui::SetNextWindowSize(ui::scale::px(ImVec2(width, height)),
                           ImGuiCond_Always);
  center_next_window();
  // A dialog keeps the window colour while menus take the brighter popup one.
  ImGui::PushStyleColor(ImGuiCol_PopupBg, styles::color(background));
  // The title bar's height is FramePadding's at Begin: a dialog's stands
  // taller than other frames.
  ImGui::PushStyleVarY(ImGuiStyleVar_FramePadding,
                       ImGui::GetStyle().FramePadding.y + ui::scale::px(2.0f));
  const bool open = ImGui::BeginPopupModal(
      title, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  if (!open) {
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

void dialog_text(std::string_view text) {
  ImFont *font = ImGui::GetFont();
  const float size = ImGui::GetFontSize();
  const float x = ImGui::GetCursorPosX();
  const float wrap_width = std::max(ImGui::GetContentRegionAvail().x, 1.0f);

  struct Line {
    const char *begin;
    const char *end;
  };
  std::vector<Line> lines;
  const char *pos = text.data();
  const char *const text_end = pos + text.size();
  do {
    const char *line_end =
        font->CalcWordWrapPosition(size, pos, text_end, wrap_width);
    if (line_end == pos && pos < text_end && *pos != '\n') {
      ++line_end; // a word wider than the line still takes a line
    }
    lines.push_back({pos, line_end});
    pos = ImTextCalcWordWrapNextLineStart(line_end, text_end);
  } while (pos < text_end);

  ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, ui::scale::px(4.0f));
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i + 1 == lines.size()) {
      ImGui::PopStyleVar();
    }
    if (i > 0) {
      ImGui::SetCursorPosX(x);
    }
    ImGui::TextUnformatted(lines[i].begin, lines[i].end);
  }
}

float dialog_button_width() { return ui::scale::px(kDialogButtonWidth); }

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
