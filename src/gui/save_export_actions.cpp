#include "save_export_actions.hpp"

#include "components/common.hpp"
#include "patches/filename_utils.hpp"
#include "patches/patch_repository.hpp"
#include "patches/patch_session.hpp"
#include <imgui.h>

namespace ui {

bool is_patch_name_valid(const ym2612::Patch &patch) {
  return !patch.name.empty() &&
         patches::sanitize_filename(patch.name) == patch.name;
}

const char *save_label_for(const patches::PatchSession &session,
                           bool is_user_patch) {
#if defined(MEGATOY_PLATFORM_WEB)
  return is_user_patch ? "Overwrite" : "Save to 'localStorage'";
#else
  if (is_user_patch) {
    return session.current_patch_path().ends_with(".ginpkg") ? "Save version"
                                                             : "Overwrite";
  }
  return "Save to 'user'";
#endif
}

void trigger_save(patches::PatchSession &session, SaveExportState &state,
                  bool force_overwrite) {
  auto result = session.save_current_patch(force_overwrite);
  if (result.is_duplicated()) {
    state.last_export_path = result.path.string();
    state.pending_popup = SaveExportState::Pending::OverwriteConfirmation;
  } else if (result.is_success()) {
    state.last_export_path = result.path.string();
    state.pending_popup = SaveExportState::Pending::SaveSuccess;
    session.repository().refresh();
    session.set_current_patch_path(
        session.repository().to_relative_path(result.path));
  } else if (result.is_error()) {
    state.last_export_error = result.error_message;
    state.pending_popup = SaveExportState::Pending::Error;
  }
}

void trigger_export(patches::PatchSession &session, SaveExportState &state,
                    patches::ExportFormat format) {
  auto result = session.export_current_patch_as(format);
  if (result.is_success()) {
    state.last_export_path = result.path.string();
    state.pending_popup = SaveExportState::Pending::ExportSuccess;
  } else if (result.is_error()) {
    state.last_export_error = result.error_message;
    state.pending_popup = SaveExportState::Pending::Error;
  }
}

void render_save_export_popups(patches::PatchSession &session,
                               SaveExportState &state) {
  const char *popup_to_open = nullptr;
  switch (state.pending_popup) {
  case SaveExportState::Pending::OverwriteConfirmation:
    popup_to_open = "Overwrite Confirmation";
    break;
  case SaveExportState::Pending::SaveSuccess:
    popup_to_open = "Save Success";
    break;
  case SaveExportState::Pending::ExportSuccess:
    popup_to_open = "Export Success";
    break;
  case SaveExportState::Pending::Error:
    popup_to_open = "Error##SaveOrExport";
    break;
  case SaveExportState::Pending::None:
    break;
  }
  if (popup_to_open) {
    ImGui::OpenPopup(popup_to_open);
    // Do not clear pending here; allow popup to open this frame.
    state.pending_popup = SaveExportState::Pending::None;
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
      auto result = session.save_current_patch(true);
      if (result.is_success()) {
        state.last_export_path = result.path.string();
        session.set_current_patch_path(
            session.repository().to_relative_path(result.path));
        state.pending_popup = SaveExportState::Pending::SaveSuccess;
      } else if (result.is_error()) {
        state.last_export_error = result.error_message;
        state.pending_popup = SaveExportState::Pending::Error;
      }
      ImGui::CloseCurrentPopup();
    }
  }

  center_next_window();
  if (ImGui::BeginPopupModal("Save Success", nullptr,
                             ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
    force_center_window();
    ImGui::Text("Patch saved successfully.");
    ImGui::TextWrapped("%s", state.last_export_path.c_str());
    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  center_next_window();
  if (ImGui::BeginPopupModal("Export Success", nullptr,
                             ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
    force_center_window();
    ImGui::Text("Patch exported successfully.");
    ImGui::TextWrapped("%s", state.last_export_path.c_str());
    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  center_next_window();
  if (ImGui::BeginPopupModal("Error##SaveOrExport", nullptr,
                             ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
    force_center_window();
    ImGui::TextWrapped("%s", state.last_export_error.c_str());
    ImGui::Spacing();
    if (ImGui::Button("OK", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

} // namespace ui
