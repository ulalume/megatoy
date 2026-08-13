#include "save_export_actions.hpp"
#include "components/common.hpp"
#include "core/status.hpp"
#include "patches/filename_utils.hpp"
#include "patches/patch_repository.hpp"
#include "patches/patch_session.hpp"
#include <imgui.h>
#include <optional>
#include <string>
#include <string_view>

namespace ui {

bool is_patch_name_valid(const ym2612::Patch &patch) {
  return !patch.name.empty() &&
         patches::sanitize_filename(patch.name) == patch.name;
}

const char *save_label_for(const patches::PatchSession &session,
                           bool is_user_patch) {
  return session.save_label_for(is_user_patch);
}

namespace {

// The path as the user knows it, not the absolute one.
std::string saved_path_label(const patches::PatchSession &session,
                             const std::filesystem::path &path) {
  const auto relative = session.repository().to_relative_path(path);
  return display_preset_path(relative.generic_string());
}

void announce_save(patches::PatchSession &session,
                   const patches::SaveResult &result) {
  if (result.is_success()) {
    megatoy::status::success("Saved " + saved_path_label(session, result.path));
  } else if (result.is_error()) {
    megatoy::status::error(result.error_message.empty() ? "Failed to save patch"
                                                        : result.error_message);
  }
}

} // namespace

void trigger_save(patches::PatchSession &session, SaveExportState &state,
                  std::string_view extension_override) {
  auto result = session.save_current_patch(extension_override);
  if (result.is_duplicated()) {
    state.overwrite_confirmation_pending = true;
    return;
  }
  if (result.is_success()) {
    session.repository().refresh();
    session.set_current_patch_path(
        session.repository().to_relative_path(result.path));
  }
  announce_save(session, result);
}

void request_save_as(SaveExportState &state) { state.save_as_requested = true; }

void render_save_export_popups(patches::PatchSession &session,
                               SaveExportState &state) {
  if (state.overwrite_confirmation_pending) {
    ImGui::OpenPopup("Overwrite Confirmation");
    state.overwrite_confirmation_pending = false;
  }
  if (state.save_as_requested) {
    ImGui::OpenPopup("Save As...");
    state.save_as_requested = false;
  }

  std::optional<std::string> selected_extension;
  if (ImGui::BeginPopup("Save As...")) {
    for (const auto &format : session.save_formats()) {
      std::string label =
          format.label.empty() ? format.extension : format.label;
      if (!format.extension.empty()) {
        label += " (" + format.extension + ")";
      }
      if (ImGui::MenuItem(label.c_str())) {
        selected_extension = format.extension;
      }
    }
    ImGui::EndPopup();
  }
  if (selected_extension) {
    announce_save(session, session.save_current_patch_as(*selected_extension));
  }

  const auto &patch = session.current_patch();

  center_next_window();
  if (ImGui::BeginPopupModal("Overwrite Confirmation", nullptr,
                             ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
    force_center_window();
    ImGui::Text("A patch with this name already exists:");
    ImGui::Text("\"%s\"", patch.name.c_str());
    ImGui::Spacing();
    ImGui::Text("Do you want to overwrite it?");
    ImGui::Spacing();

    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    const bool overwrite_button = ImGui::Button("Overwrite", ImVec2(120, 0));

    ImGui::EndPopup();

    if (overwrite_button) {
      auto result = session.save_current_patch();
      if (result.is_success()) {
        session.set_current_patch_path(
            session.repository().to_relative_path(result.path));
      }
      announce_save(session, result);
      ImGui::CloseCurrentPopup();
    }
  }
}

} // namespace ui
