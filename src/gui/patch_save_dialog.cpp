#include "patch_save_dialog.hpp"
#include "gui/components/modal.hpp"
#include "gui/save_as_dialog.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "patches/filename_utils.hpp"

#include <algorithm>
#include <cstring>
#include <imgui.h>

namespace ui {
namespace {

std::string filename_error(const std::string &stem) {
  if (stem.empty()) {
    return "Filename cannot be empty.";
  }
  if (patches::sanitize_filename(stem) != stem) {
    return "Filename contains invalid characters.";
  }
  return {};
}

/// Both rows, each starting at the left edge so the labels ImGui puts to
/// their right line up too.
float fields_width() {
  float label_width = 0.0f;
  for (const char *label : {"Filename", "Folder"}) {
    label_width = std::max(label_width, ImGui::CalcTextSize(label).x);
  }
  return ImGui::GetContentRegionAvail().x - label_width -
         ImGui::GetStyle().ItemInnerSpacing.x;
}

} // namespace

void render_patch_save_dialog(
    const char *title, const char *primary_label, PatchSaveDialogState &state,
    const std::vector<formats::SaveFormatInfo> &formats,
    const megatoy::workspace::Workspace &workspace,
    const PatchSaveDialogAction &on_save) {
  if (state.requested) {
    state.requested = false;
    state.open = true;
    ImGui::OpenPopup(title);
  }
  if (!state.open) {
    return;
  }

  // Escape cancels, but only once the text field has let go of it -- see
  // escape_pressed() in modal.cpp.
  // Wider than the other dialogs: the name shares its row with the format.
  auto modal = begin_modal(title, ModalDismiss::Escape, kDialogWidth * 1.25f);
  bool cancelled = modal.dismissed;
  bool save = false;
  bool overwrites = false;
  if (modal.visible) {
    const float row_width = fields_width() - ImGui::GetStyle().ItemSpacing.x;
    // The format combo is as wide as its longest name, so none is cut short;
    // the name field takes what is left.
    float format_width = 0.0f;
    for (const auto &format : formats) {
      format_width = std::max(
          format_width, ImGui::CalcTextSize(format.display_name().c_str()).x);
    }
    format_width +=
        ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFrameHeight();
    const float name_width =
        std::max(row_width - format_width, row_width * 0.3f);

    char input[512];
    std::strncpy(input, state.stem.c_str(), sizeof(input) - 1);
    input[sizeof(input) - 1] = '\0';
    if (ImGui::IsWindowAppearing()) {
      ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(name_width);
    const bool entered =
        ImGui::InputText("##patch_save_stem", input, sizeof(input),
                         ImGuiInputTextFlags_EnterReturnsTrue |
                             ImGuiInputTextFlags_AutoSelectAll);
    state.stem = input;

    std::string format_preview = state.extension;
    for (const auto &format : formats) {
      if (format.extension == state.extension) {
        format_preview = format.display_name();
        break;
      }
    }
    // The label belongs to the row, so it rides on the last field in it.
    ImGui::SameLine();
    ImGui::SetNextItemWidth(row_width - name_width);
    if (ImGui::BeginCombo("Filename", format_preview.c_str())) {
      for (const auto &format : formats) {
        const bool selected = format.extension == state.extension;
        if (ImGui::Selectable(format.display_name().c_str(), selected)) {
          state.extension = format.extension;
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    const auto choices = save_as_folder_choices(workspace.folders());
    if (save_as_shows_folder_choice(choices)) {
      std::string folder_preview = state.folder.filename().string();
      for (const auto &folder : choices) {
        if (folder.path == state.folder) {
          folder_preview = folder.name;
          break;
        }
      }
      ImGui::SetNextItemWidth(fields_width());
      if (ImGui::BeginCombo("Folder", folder_preview.c_str())) {
        for (const auto &folder : choices) {
          // Two folders can carry the same display name.
          ImGui::PushID(folder.path.string().c_str());
          const bool selected = folder.path == state.folder;
          if (ImGui::Selectable(folder.name.c_str(), selected)) {
            state.folder = folder.path;
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
          ImGui::PopID();
        }
        ImGui::EndCombo();
      }
    }

    const std::string error = filename_error(state.stem);
    overwrites = error.empty() && !state.folder.empty() &&
                 std::filesystem::exists(save_as_target_path(
                     state.folder, state.stem, state.extension));
    const ImVec4 warning = styles::color(styles::MegatoyCol::StatusWarning);
    if (!error.empty()) {
      ImGui::TextColored(warning, "%s", error.c_str());
    } else if (overwrites) {
      ImGui::TextColored(warning, "%s",
                         "A patch with this name exists and will be "
                         "overwritten.");
    }
    save = entered && error.empty();

    ImGui::Spacing();
    const float width = dialog_button_width();
    align_buttons_right({width, width});
    if (ImGui::Button("Cancel", ImVec2(width, 0))) {
      cancelled = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!error.empty());
    if (ImGui::Button(overwrites ? "Overwrite" : primary_label,
                      ImVec2(width, 0))) {
      save = true;
    }
    ImGui::EndDisabled();

    if (save || (cancelled && !modal.dismissed)) {
      ImGui::CloseCurrentPopup();
    }
    end_modal();
  }

  if (!save && !cancelled) {
    return;
  }
  state.open = false;
  if (cancelled) {
    return;
  }
  if (on_save) {
    on_save(state.folder, state.stem, state.extension, overwrites);
  }
}

} // namespace ui
