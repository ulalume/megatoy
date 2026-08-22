#include "changelog_dialog.hpp"

#include "changelog.hpp"
#include "common.hpp"
#include "gui/styles/megatoy_style.hpp"

#include <imgui.h>
#include <string>

namespace ui {
namespace {

constexpr const char *kTitle = "Change Log";
bool g_open_requested = false;

} // namespace

void open_changelog_dialog() { g_open_requested = true; }

void render_changelog_dialog() {
  if (g_open_requested) {
    g_open_requested = false;
    ImGui::OpenPopup(kTitle);
  }

  // The only dialog long enough to need scrolling, so the only one with a
  // fixed size rather than AlwaysAutoResize.
  ImGui::SetNextWindowSize(ImVec2(460.0f, 420.0f), ImGuiCond_Appearing);
  center_next_window();
  if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_NoResize |
                                                   ImGuiWindowFlags_NoMove)) {
    return;
  }
  force_center_window();

  const float footer = ImGui::GetFrameHeightWithSpacing() +
                       ImGui::GetStyle().ItemSpacing.y +
                       ImGui::GetStyle().WindowPadding.y;
  if (ImGui::BeginChild("##entries", ImVec2(0.0f, -footer))) {
    for (const auto &entry : megatoy::changelog()) {
      ImGui::SeparatorText(std::string(entry.version).c_str());
      for (const auto &item : entry.items) {
        ImGui::Bullet();
        ImGui::TextWrapped("%.*s", static_cast<int>(item.text.size()),
                           item.text.data());
        if (item.details.empty()) {
          continue;
        }
        ImGui::Indent();
        for (const auto &detail : item.details) {
          ImGui::Bullet();
          ImGui::PushStyleColor(ImGuiCol_Text,
                                styles::color(styles::MegatoyCol::TextMuted));
          ImGui::TextWrapped("%.*s", static_cast<int>(detail.size()),
                             detail.data());
          ImGui::PopStyleColor();
        }
        ImGui::Unindent();
      }
      ImGui::Spacing();
    }
  }
  ImGui::EndChild();

  ImGui::Separator();
  if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) {
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}

} // namespace ui
