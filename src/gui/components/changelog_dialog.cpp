#include "changelog_dialog.hpp"

#include "changelog.hpp"
#include "common.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "modal.hpp"

#include <imgui.h>
#include <string>

namespace ui {
namespace {

constexpr const char *kTitle = "Change Log";
bool g_open_requested = false;
/// Set as the dialog opens: each version's section is put back to its
/// default, the newest open and the rest closed.
bool g_reset_sections = false;

} // namespace

void open_changelog_dialog() { g_open_requested = true; }

void render_changelog_dialog() {
  if (g_open_requested) {
    g_open_requested = false;
    g_reset_sections = true;
    ImGui::OpenPopup(kTitle);
  }

  // The only dialog long enough to need scrolling, so the only one with a
  // fixed size rather than AlwaysAutoResize.
  // The one dialog that scrolls, so it needs a height of its own rather than
  // one taken from however much it happens to hold today.
  if (!begin_modal(kTitle, ModalDismiss::EscapeOrOutsideClick, 460.0f, 420.0f)
           .visible) {
    return;
  }

  const float footer = ImGui::GetFrameHeightWithSpacing() +
                       ImGui::GetStyle().ItemSpacing.y +
                       ImGui::GetStyle().WindowPadding.y;
  if (ImGui::BeginChild("##entries", ImVec2(0.0f, -footer))) {
    bool newest = true;
    for (const auto &entry : megatoy::changelog()) {
      if (g_reset_sections) {
        ImGui::SetNextItemOpen(newest);
      }
      newest = false;
      if (!ImGui::CollapsingHeader(std::string(entry.version).c_str())) {
        continue;
      }
      for (const auto &item : entry.items) {
        ImGui::Bullet();
        dialog_text(item.text);
        if (item.details.empty()) {
          continue;
        }
        ImGui::Indent();
        for (const auto &detail : item.details) {
          ImGui::Bullet();
          ImGui::PushStyleColor(ImGuiCol_Text,
                                styles::color(styles::MegatoyCol::TextMuted));
          dialog_text(detail);
          ImGui::PopStyleColor();
        }
        ImGui::Unindent();
      }
      ImGui::Spacing();
    }
    g_reset_sections = false;
  }
  ImGui::EndChild();

  ImGui::Separator();
  align_buttons_right({dialog_button_width()});
  if (ImGui::Button("Close", ImVec2(dialog_button_width(), 0.0f))) {
    ImGui::CloseCurrentPopup();
  }

  end_modal();
}

} // namespace ui
