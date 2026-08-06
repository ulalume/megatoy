#include "save_export_actions.hpp"
#include "components/common.hpp"
#include "core/status.hpp"
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

void trigger_export(patches::PatchSession &session, SaveExportState &state,
                    const patches::ExportFormatInfo &format) {
  (void)state;
  auto result = session.export_current_patch_as(format);
  if (result.is_success()) {
    // The web build hands the file to the browser instead of a path.
    megatoy::status::success(result.path == "download"
                                 ? "Download started."
                                 : "Exported " + result.path.string());
  } else if (result.is_error()) {
    megatoy::status::error(result.error_message.empty()
                               ? "Failed to export patch"
                               : result.error_message);
  }
}

void render_save_export_popups(patches::PatchSession &session,
                               SaveExportState &state) {
  if (state.overwrite_confirmation_pending) {
    ImGui::OpenPopup("Overwrite Confirmation");
    state.overwrite_confirmation_pending = false;
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
      // A duplicate is a NEW file. Routing this through the ordinary save
      // would overwrite the original in place, since the current patch still
      // carries the original's path -- which is exactly the bug this
      // replaced: "Duplicate" quietly rewrote the source file and created
      // nothing.
      auto result = session.duplicate_current_patch(state.duplicate.name);
      announce_save(session, result);
      state.duplicate.open = false;
      ImGui::CloseCurrentPopup();
    }
    if (disable_save)
      ImGui::EndDisabled();

    ImGui::EndPopup();
  }
}

} // namespace ui
