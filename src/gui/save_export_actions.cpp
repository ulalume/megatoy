#include "save_export_actions.hpp"
#include "components/common.hpp"
#include "components/modal.hpp"
#include "core/status.hpp"
#include "patches/filename_utils.hpp"
#include "patches/patch_repository.hpp"
#include "patches/patch_session.hpp"
#include "platform/platform_config.hpp"
#include <filesystem>
#include <imgui.h>
#include <optional>
#include <string>
#include <string_view>
#if defined(MEGATOY_PLATFORM_WEB)
#include "gui/save_as_dialog.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "platform/web/web_storage_bootstrap.hpp"
#include "platform/web/web_workspace_download.hpp"
#include <algorithm>
#include <cstring>
#endif

namespace ui {

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

std::string save_as_stem_suggestion(const patches::PatchSession &session) {
  if (!session.current_patch_path().empty()) {
    const auto extension = std::filesystem::path(session.current_patch_path())
                               .extension()
                               .string();
    const bool imported_container_child =
        extension != ".ginpkg" &&
        session.current_patch_selection_path() != session.current_patch_path();
    if (!imported_container_child) {
      return std::filesystem::path(session.current_patch_path())
          .stem()
          .string();
    }
  }
  const auto &imported_name = session.current_patch().name;
  const auto sanitized = patches::sanitize_filename(imported_name);
  return sanitized.empty() ? "patch" : sanitized;
}

#if defined(MEGATOY_PLATFORM_WEB)

constexpr const char *kSaveAsMenuId = "##save_as_menu";
constexpr const char *kSaveToStorageTitle = "Save to browser storage";

std::string save_as_filename_error(const std::string &stem) {
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
float save_as_fields_width() {
  float label_width = 0.0f;
  for (const char *label : {"Filename", "Folder"}) {
    label_width = std::max(label_width, ImGui::CalcTextSize(label).x);
  }
  return ImGui::GetContentRegionAvail().x - label_width -
         ImGui::GetStyle().ItemInnerSpacing.x;
}

void open_save_to_storage_dialog(patches::PatchSession &session,
                                 SaveExportState &state) {
  auto &dialog = state.save_as_dialog;
  dialog.stem = save_as_stem_suggestion(session);
  dialog.extension = default_save_as_extension(
      std::filesystem::path(session.current_patch_path()).extension().string(),
      session.save_formats());

  const auto choices = save_as_folder_choices(session.repository().workspace());
  const auto preferred =
      default_save_as_folder(session.writable_source_folder(),
                             platform::web::default_workspace_folder());
  const bool offered =
      std::any_of(choices.begin(), choices.end(),
                  [&](const auto &folder) { return folder.path == preferred; });
  // Empty when there is nowhere to write: the save itself says so.
  dialog.folder = choices.empty() ? std::filesystem::path{}
                  : offered       ? preferred
                                  : choices.front().path;
  dialog.open = true;
  ImGui::OpenPopup(kSaveToStorageTitle);
}

void download_current_patch(patches::PatchSession &session,
                            const std::string &extension) {
  if (platform::web::download_patch(session.current_patch(), extension,
                                    save_as_stem_suggestion(session))) {
    megatoy::status::success("Download started.");
  } else {
    megatoy::status::error("Failed to prepare " + extension + " download.");
  }
}

/// The two ways out of the editor, under the button that asked for them.
void render_save_as_menu(patches::PatchSession &session,
                         SaveExportState &state) {
  if (state.save_as_menu_anchor) {
    ImGui::SetNextWindowPos(*state.save_as_menu_anchor);
  }
  if (!ImGui::BeginPopup(kSaveAsMenuId)) {
    return;
  }

  std::optional<std::string> download_extension;
  bool save_to_storage = false;
  if (ImGui::BeginMenu("Download...")) {
    for (const auto &format : session.save_formats()) {
      if (ImGui::MenuItem(format.display_name().c_str())) {
        download_extension = format.extension;
      }
    }
    ImGui::EndMenu();
  }
  if (ImGui::MenuItem("Save to browser storage...")) {
    save_to_storage = true;
  }
  ImGui::EndPopup();

  if (download_extension) {
    download_current_patch(session, *download_extension);
  }
  if (save_to_storage) {
    open_save_to_storage_dialog(session, state);
  }
}

/// Write the patch, with the Overwrite Confirmation carrying the folder when
/// the name turns out to be taken after all.
void save_as_into(patches::PatchSession &session, SaveExportState &state,
                  bool overwrite) {
  const auto &dialog = state.save_as_dialog;
  auto result = overwrite ? session.save_current_patch_as_in_forced(
                                dialog.folder, dialog.extension, dialog.stem)
                          : session.save_current_patch_as_in(
                                dialog.folder, dialog.extension, dialog.stem);
  if (result.is_duplicated()) {
    state.pending_save_as_extension = dialog.extension;
    state.pending_save_as_stem = dialog.stem;
    state.pending_save_as_folder = dialog.folder;
    state.overwrite_confirmation_pending = true;
    return;
  }
  announce_save(session, result);
}

/**
 * Name, format and folder for one patch kept in browser storage.
 *
 * The browser has no file dialog to fall back on, so everything the desktop
 * one would ask is asked here instead.
 */
void render_save_to_storage_dialog(patches::PatchSession &session,
                                   SaveExportState &state) {
  auto &dialog = state.save_as_dialog;
  if (!dialog.open) {
    return;
  }

  // Escape cancels, but only once the text field has let go of it -- see
  // escape_pressed() in modal.cpp.
  // Wider than the other dialogs: the name shares its row with the format.
  auto modal = begin_modal(kSaveToStorageTitle, ModalDismiss::Escape,
                           kDialogWidth * 1.25f);
  bool cancelled = modal.dismissed;
  bool save = false;
  bool overwrites = false;
  if (modal.visible) {
    const float fields_width = save_as_fields_width();
    const float row_width = fields_width - ImGui::GetStyle().ItemSpacing.x;
    const auto formats = session.save_formats();
    // The format combo is as wide as its longest name, so none is cut short;
    // the name field takes what is left.
    float format_width = 0.0f;
    for (const auto &format : formats) {
      format_width = std::max(
          format_width, ImGui::CalcTextSize(format.display_name().c_str()).x);
    }
    format_width += ImGui::GetStyle().FramePadding.x * 2.0f +
                    ImGui::GetFrameHeight();
    const float name_width = std::max(row_width - format_width, row_width * 0.3f);

    char input[512];
    std::strncpy(input, dialog.stem.c_str(), sizeof(input) - 1);
    input[sizeof(input) - 1] = '\0';
    if (ImGui::IsWindowAppearing()) {
      ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(name_width);
    const bool entered =
        ImGui::InputText("##save_as_stem", input, sizeof(input),
                         ImGuiInputTextFlags_EnterReturnsTrue |
                             ImGuiInputTextFlags_AutoSelectAll);
    dialog.stem = input;

    std::string format_preview = dialog.extension;
    for (const auto &format : formats) {
      if (format.extension == dialog.extension) {
        format_preview = format.display_name();
        break;
      }
    }
    // The label belongs to the row, so it rides on the last field in it.
    ImGui::SameLine();
    ImGui::SetNextItemWidth(row_width - name_width);
    if (ImGui::BeginCombo("Filename", format_preview.c_str())) {
      for (const auto &format : formats) {
        const bool selected = format.extension == dialog.extension;
        if (ImGui::Selectable(format.display_name().c_str(), selected)) {
          dialog.extension = format.extension;
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    const auto choices =
        save_as_folder_choices(session.repository().workspace());
    if (save_as_shows_folder_choice(choices)) {
      std::string folder_preview = dialog.folder.filename().string();
      for (const auto &folder : choices) {
        if (folder.path == dialog.folder) {
          folder_preview = folder.name;
          break;
        }
      }
      ImGui::SetNextItemWidth(fields_width);
      if (ImGui::BeginCombo("Folder", folder_preview.c_str())) {
        for (const auto &folder : choices) {
          // Two folders can carry the same display name.
          ImGui::PushID(folder.path.string().c_str());
          const bool selected = folder.path == dialog.folder;
          if (ImGui::Selectable(folder.name.c_str(), selected)) {
            dialog.folder = folder.path;
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
          ImGui::PopID();
        }
        ImGui::EndCombo();
      }
    }

    const std::string error = save_as_filename_error(dialog.stem);
    overwrites = error.empty() && !dialog.folder.empty() &&
                 std::filesystem::exists(save_as_target_path(
                     dialog.folder, dialog.stem, dialog.extension));
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
    if (ImGui::Button(overwrites ? "Overwrite" : "Save", ImVec2(width, 0))) {
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
  dialog.open = false;
  if (cancelled) {
    return;
  }
  save_as_into(session, state, overwrites);
}

#endif

void clear_pending_save_as(SaveExportState &state) {
  state.pending_save_as_extension.reset();
  state.pending_save_as_stem.reset();
  state.pending_save_as_folder.reset();
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
#if defined(MEGATOY_PLATFORM_WEB)
    ImGui::OpenPopup(kSaveAsMenuId);
#else
    ImGui::OpenPopup("Save As...");
#endif
    state.save_as_requested = false;
  }

#if defined(MEGATOY_PLATFORM_WEB)
  render_save_as_menu(session, state);
  render_save_to_storage_dialog(session, state);
#else
  std::optional<std::string> selected_extension;
  if (ImGui::BeginPopup("Save As...")) {
    for (const auto &format : session.save_formats()) {
      if (ImGui::MenuItem(format.display_name().c_str())) {
        selected_extension = format.extension;
      }
    }
    ImGui::EndPopup();
  }
  if (selected_extension) {
    auto result = session.save_current_patch_as(*selected_extension);
    if (result.is_duplicated()) {
      state.pending_save_as_extension = *selected_extension;
      state.overwrite_confirmation_pending = true;
    } else {
      announce_save(session, result);
    }
  }
#endif

  // Escape cancels: overwriting destroys the file that is already there, so
  // it only happens when it is asked for.
  auto overwrite = begin_modal("Overwrite Confirmation", ModalDismiss::Escape);
  if (overwrite.dismissed) {
    clear_pending_save_as(state);
  }
  if (overwrite.visible) {
    ImGui::Text("A patch with this name already exists:");
    const std::string overwrite_stem =
        state.pending_save_as_stem.value_or(save_as_stem_suggestion(session));
    const std::string overwrite_extension =
        state.pending_save_as_extension.value_or("");
    ImGui::Text("\"%s%s\"", overwrite_stem.c_str(),
                overwrite_extension.c_str());
    ImGui::Spacing();
    ImGui::Text("Do you want to overwrite it?");
    ImGui::Spacing();

    align_buttons_right({dialog_button_width(), dialog_button_width()});
    const bool cancel_button =
        ImGui::Button("Cancel", ImVec2(dialog_button_width(), 0));
    if (cancel_button) {
      clear_pending_save_as(state);
      ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    const bool overwrite_button =
        ImGui::Button("Overwrite", ImVec2(dialog_button_width(), 0));
    if (overwrite_button) {
      ImGui::CloseCurrentPopup();
    }

    end_modal();

    if (overwrite_button) {
      const auto pending_extension = state.pending_save_as_extension;
      const auto pending_stem = state.pending_save_as_stem;
      const auto pending_folder = state.pending_save_as_folder;
      clear_pending_save_as(state);
      patches::SaveResult result = patches::SaveResult::cancelled();
      if (!pending_extension) {
        result = session.save_current_patch();
      } else if (pending_folder) {
        result = session.save_current_patch_as_in_forced(
            *pending_folder, *pending_extension, pending_stem.value_or(""));
      } else {
        result = session.save_current_patch_as_forced(
            *pending_extension, pending_stem.value_or(""));
      }
      if (result.is_success()) {
        session.set_current_patch_path(
            session.repository().to_relative_path(result.path));
      }
      announce_save(session, result);
    }
  }
}

} // namespace ui
