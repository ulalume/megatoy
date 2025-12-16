#include "save_export_actions.hpp"
#include "components/common.hpp"
#include "patches/filename_utils.hpp"
#include "patches/patch_repository.hpp"
#include "patches/patch_session.hpp"
#include <algorithm>
#include <imgui.h>
#include <iomanip>
#include <sstream>
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

void trigger_save(patches::PatchSession &session, SaveExportState &state,
                  bool force_overwrite, std::string_view extension_override) {
  auto result =
      session.save_current_patch(force_overwrite, extension_override);
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
                    const patches::ExportFormatInfo &format) {
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

namespace {

std::string format_number(int number) {
  std::ostringstream oss;
  oss << std::setw(2) << std::setfill('0') << number;
  return oss.str();
}

std::string base_name_without_counter(const std::string &name, int &start) {
  start = 2;
  auto pos = name.find_last_of(' ');
  if (pos != std::string::npos && pos + 1 < name.size()) {
    std::string suffix = name.substr(pos + 1);
    bool all_digits =
        !suffix.empty() && std::all_of(suffix.begin(), suffix.end(), ::isdigit);
    if (all_digits) {
      try {
        int parsed = std::stoi(suffix);
        if (parsed >= 1) {
          start = parsed + 1;
          return name.substr(0, pos);
        }
      } catch (...) {
      }
    }
  }
  return name;
}

bool name_conflicts(const patches::PatchSession &session,
                    const std::string &name) {
  return session.repository().patch_name_conflicts(name);
}

std::string generate_duplicate_name(const patches::PatchSession &session) {
  const std::string current = session.current_patch().name;
  int counter = 0;
  auto base = base_name_without_counter(current, counter);
  if (base.empty()) {
    base = "patch";
  }

  std::string candidate;
  while (true) {
    candidate = base + " " + format_number(counter);
    if (!name_conflicts(session, candidate)) {
      break;
    }
    ++counter;
  }
  return candidate;
}

} // namespace

void start_duplicate_dialog(patches::PatchSession &session,
                            SaveExportState &state) {
  state.duplicate.open = true;
  state.duplicate.name = generate_duplicate_name(session);
}

void render_duplicate_dialog(patches::PatchSession &session,
                             SaveExportState &state) {
  if (!state.duplicate.open) {
    return;
  }

  ImGui::OpenPopup("Duplicate Patch");
  if (ImGui::BeginPopupModal("Duplicate Patch", &state.duplicate.open,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    char buffer[128];
    std::strncpy(buffer, state.duplicate.name.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    ImGui::Text("Enter a new name for the duplicate:");
    ImGui::InputText("##dup_name", buffer, sizeof(buffer));
    state.duplicate.name = buffer;

    ym2612::Patch temp = session.current_patch();
    temp.name = state.duplicate.name;
    bool name_valid = is_patch_name_valid(temp);
    bool exists = name_conflicts(session, state.duplicate.name);
    bool disable_save = !name_valid || exists || state.duplicate.name.empty();

    if (exists) {
      ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "Name already exists");
    } else if (!name_valid) {
      ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "Invalid name");
    }

    ImGui::Spacing();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      state.duplicate.open = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (disable_save)
      ImGui::BeginDisabled(true);
    if (ImGui::Button("Save", ImVec2(120, 0))) {
      auto original_name = session.current_patch().name;
      auto patch_copy = session.current_patch();
      patch_copy.name = state.duplicate.name;
      session.set_current_patch(patch_copy, session.current_patch_path());
      // Duplicates are always saved as packaged ginpkg files.
      trigger_save(session, state, false, ".ginpkg");
      state.duplicate.open = false;
      ImGui::CloseCurrentPopup();
    }
    if (disable_save)
      ImGui::EndDisabled();

    ImGui::EndPopup();
  }
}

} // namespace ui
