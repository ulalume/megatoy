#include "status_toasts.hpp"

#include "core/status.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"

#include "gui/fonts/megatoy_icons.hpp"
#include <chrono>
#include <functional>
#include <imgui.h>
#include <string>

namespace ui {

namespace {

using megatoy::status::Severity;

constexpr float kToastWidth = 340.0f;
constexpr float kMargin = 12.0f;
constexpr int kMaxVisible = 5;
constexpr std::chrono::seconds kAutoDismiss{4};

const char *severity_icon(Severity severity) {
  switch (severity) {
  case Severity::Success:
    return ICON_MEGATOY_CIRCLE_CHECK;
  case Severity::Warning:
    return ICON_MEGATOY_TRIANGLE_EXCLAMATION;
  case Severity::Error:
    return ICON_MEGATOY_CIRCLE_XMARK;
  case Severity::Info:
    break;
  }
  return ICON_MEGATOY_CIRCLE_INFO;
}

ImVec4 severity_color(Severity severity) {
  switch (severity) {
  case Severity::Success:
    return styles::color(styles::MegatoyCol::StatusSuccess);
  case Severity::Warning:
    return styles::color(styles::MegatoyCol::StatusWarning);
  case Severity::Error:
    return styles::color(styles::MegatoyCol::StatusError);
  case Severity::Info:
    break;
  }
  return styles::color(styles::MegatoyCol::TextMuted);
}

bool sticky(const megatoy::status::Entry &entry) {
  // Four seconds is enough to notice a result, not to act on an offer.
  return entry.severity == Severity::Warning ||
         entry.severity == Severity::Error ||
         (entry.action.valid() && entry.action.lasts.count() == 0);
}

std::chrono::steady_clock::duration
lifetime(const megatoy::status::Entry &entry) {
  if (entry.action.valid() && entry.action.lasts.count() > 0) {
    return entry.action.lasts;
  }
  return kAutoDismiss;
}

} // namespace

void render_status_toasts() {
  const auto now = std::chrono::steady_clock::now();
  auto entries = megatoy::status::entries();
  // The toast under the pointer, which waits to expire until it is left.
  static std::uint64_t hovered_id = 0;
  const std::uint64_t was_hovered = hovered_id;
  hovered_id = 0;

  // Expire transient toasts here rather than in the service, so an entry
  // posted while the tab was hidden still gets its time on screen.
  std::erase_if(entries, [&](const megatoy::status::Entry &entry) {
    if (!sticky(entry) && entry.id != was_hovered &&
        now - entry.posted_at > lifetime(entry)) {
      megatoy::status::dismiss(entry.id);
      return true;
    }
    return false;
  });
  if (entries.empty()) {
    return;
  }

  // Newest first, capped so a burst cannot wallpaper the screen.
  if (entries.size() > kMaxVisible) {
    entries.erase(entries.begin(),
                  entries.end() - static_cast<long>(kMaxVisible));
  }

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const float margin = ui::scale::px(kMargin);
  const ImVec2 corner(viewport->WorkPos.x + viewport->WorkSize.x - margin,
                      viewport->WorkPos.y + viewport->WorkSize.y - margin);

  ImGui::SetNextWindowPos(corner, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
  // NoBackground, not a transparent fill: the fill alone still left the
  // host's own border drawn around the stack.
  const ImGuiWindowFlags host_flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;
  if (!ImGui::Begin("##status_toasts", nullptr, host_flags)) {
    ImGui::End();
    return;
  }

  // After the loop: an action may post its own toast.
  std::function<void()> action_to_perform;

  // Oldest at the top, newest nearest the corner.
  for (const auto &entry : entries) {
    ImGui::PushID(static_cast<int>(entry.id));

    const ImVec4 accent = severity_color(entry.severity);
    ImGui::PushStyleColor(ImGuiCol_Border, accent);
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);

    const float toast_width = ui::scale::px(kToastWidth);
    if (ImGui::BeginChild("toast", ImVec2(toast_width, 0.0f),
                          ImGuiChildFlags_Borders |
                              ImGuiChildFlags_AutoResizeY |
                              ImGuiChildFlags_AlwaysUseWindowPadding)) {
      // A close mark in the corner, saying what a click on the toast does.
      const ImGuiStyle &style = ImGui::GetStyle();
      const float close_width = ImGui::CalcTextSize(ICON_MEGATOY_XMARK).x;
      const ImVec2 close_pos(ImGui::GetWindowPos().x + toast_width -
                                 style.WindowPadding.x - close_width,
                             ImGui::GetWindowPos().y + style.WindowPadding.y);
      const bool hovered =
          ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
      if (hovered) {
        hovered_id = entry.id;
      }
      ImGui::GetWindowDrawList()->AddText(
          close_pos,
          hovered ? ImGui::GetColorU32(ImGuiCol_Text)
                  : styles::color_u32(styles::MegatoyCol::TextMuted),
          ICON_MEGATOY_XMARK);

      ImGui::TextColored(accent, "%s", severity_icon(entry.severity));
      ImGui::SameLine();
      ImGui::PushTextWrapPos(toast_width - style.WindowPadding.x * 2.0f -
                             close_width - style.ItemSpacing.x);
      ImGui::TextUnformatted(entry.message.c_str());
      ImGui::PopTextWrapPos();

      if (entry.action.valid()) {
        ImGui::Spacing();
        if (ImGui::Button(entry.action.label.c_str())) {
          action_to_perform = entry.action.perform;
          megatoy::status::dismiss(entry.id);
        }
      }
    }
    ImGui::EndChild();

    // Everywhere the button is not, the toast is its own close button.
    if (ImGui::IsItemClicked()) {
      megatoy::status::dismiss(entry.id);
    }
    if (ImGui::IsItemHovered() && sticky(entry)) {
      ImGui::SetTooltip("Click to dismiss");
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
  }

  ImGui::End();

  if (action_to_perform) {
    action_to_perform();
  }
}

} // namespace ui
