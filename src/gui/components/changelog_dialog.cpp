#include "changelog_dialog.hpp"

#include "changelog.hpp"
#include "common.hpp"
#include "gui/components/section_header.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "modal.hpp"

#include <deque>
#include <imgui.h>
#include <string>

namespace ui {
namespace {

constexpr const char *kTitle = "Change Log";
bool g_open_requested = false;
/// Set as the dialog opens: each version's section is put back to its
/// default, the newest open and the rest closed.
bool g_reset_sections = false;
/// Whether each version's section is open, newest first.
std::deque<bool> g_section_open;

} // namespace

void open_changelog_dialog() { g_open_requested = true; }

void render_changelog_dialog() {
  if (g_open_requested) {
    g_open_requested = false;
    g_reset_sections = true;
    ImGui::OpenPopup(kTitle);
  }

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
    const auto &entries = megatoy::changelog();
    if (g_reset_sections || g_section_open.size() != entries.size()) {
      g_section_open.assign(entries.size(), false);
      if (!g_section_open.empty()) {
        g_section_open.front() = true;
      }
    }
    g_reset_sections = false;
    for (std::size_t i = 0; i < entries.size(); ++i) {
      const auto &entry = entries[i];
      // Headed like the patch editor's sections.
      const std::string version(entry.version);
      if (!section_header(version.c_str(), g_section_open[i])) {
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
      section_end(version.c_str());
      ImGui::Spacing();
    }
    scroll_reveal::step();
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
