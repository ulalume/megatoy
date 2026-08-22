#include "status_toasts.hpp"

#include "core/status.hpp"
#include "gui/styles/megatoy_style.hpp"

#include <IconsFontAwesome7.h>
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
    return ICON_FA_CIRCLE_CHECK;
  case Severity::Warning:
    return ICON_FA_TRIANGLE_EXCLAMATION;
  case Severity::Error:
    return ICON_FA_CIRCLE_XMARK;
  case Severity::Info:
    break;
  }
  return ICON_FA_CIRCLE_INFO;
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
  // A toast offering an action waits to be answered. Four seconds is long
  // enough to notice a result and too short to read an offer and act on it.
  return entry.severity == Severity::Warning ||
         entry.severity == Severity::Error || entry.action.valid();
}

} // namespace

void render_status_toasts() {
  const auto now = std::chrono::steady_clock::now();
  auto entries = megatoy::status::entries();

  // Expire transient toasts here rather than in the service, so an entry
  // posted while the tab was hidden still gets its time on screen.
  std::erase_if(entries, [&](const megatoy::status::Entry &entry) {
    if (!sticky(entry) && now - entry.posted_at > kAutoDismiss) {
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
  const ImVec2 corner(viewport->WorkPos.x + viewport->WorkSize.x - kMargin,
                      viewport->WorkPos.y + viewport->WorkSize.y - kMargin);

  ImGui::SetNextWindowPos(corner, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
  // NoBackground rather than a transparent background colour: the host is
  // only a place to stack the toasts in, and a transparent fill still left
  // the window's own border drawn around the lot of them.
  const ImGuiWindowFlags host_flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
      ImGuiWindowFlags_NoDocking;
  if (!ImGui::Begin("##status_toasts", nullptr, host_flags)) {
    ImGui::End();
    return;
  }

  // Run after the loop: the action may post its own toast, and mutating the
  // list mid-iteration would drop one.
  std::function<void()> action_to_perform;

  // Oldest at the top, newest nearest the corner.
  for (const auto &entry : entries) {
    ImGui::PushID(static_cast<int>(entry.id));

    const ImVec4 accent = severity_color(entry.severity);
    ImGui::PushStyleColor(ImGuiCol_Border, accent);
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);

    if (ImGui::BeginChild("toast", ImVec2(kToastWidth, 0.0f),
                          ImGuiChildFlags_Borders |
                              ImGuiChildFlags_AutoResizeY |
                              ImGuiChildFlags_AlwaysUseWindowPadding)) {
      ImGui::TextColored(accent, "%s", severity_icon(entry.severity));
      ImGui::SameLine();
      ImGui::PushTextWrapPos(kToastWidth -
                             ImGui::GetStyle().WindowPadding.x * 2.0f);
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
