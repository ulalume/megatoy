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
#include "gui/patch_save_dialog.hpp"
#include "gui/save_as_dialog.hpp"
#include "platform/web/web_storage_bootstrap.hpp"
#include "platform/web/web_workspace_download.hpp"
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

void open_save_to_storage_dialog(patches::PatchSession &session,
                                 SaveExportState &state) {
  auto &dialog = state.save_as_dialog;
  dialog.stem = save_as_stem_suggestion(session);
  dialog.extension = default_save_as_extension(
      std::filesystem::path(session.current_patch_path()).extension().string(),
      session.save_formats());

  dialog.folder = save_as_initial_folder(
      save_as_folder_choices(session.repository().workspace()),
      default_save_as_folder(session.writable_source_folder(),
                             platform::web::default_workspace_folder()));
  dialog.requested = true;
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

  const auto download_extension = download_format_menu(session);
  bool save_to_storage = false;
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
                  const std::filesystem::path &folder, const std::string &stem,
                  const std::string &extension, bool overwrite) {
  auto result =
      overwrite
          ? session.save_current_patch_as_in_forced(folder, extension, stem)
          : session.save_current_patch_as_in(folder, extension, stem);
  if (result.is_duplicated()) {
    state.pending_save_as_extension = extension;
    state.pending_save_as_stem = stem;
    state.pending_save_as_folder = folder;
    state.overwrite_confirmation_pending = true;
    return;
  }
  announce_save(session, result);
}

#endif

void clear_pending_save_as(SaveExportState &state) {
  state.pending_save_as_extension.reset();
  state.pending_save_as_stem.reset();
  state.pending_save_as_folder.reset();
}

} // namespace

#if defined(MEGATOY_PLATFORM_WEB)
void download_current_patch(patches::PatchSession &session,
                            const std::string &extension) {
  if (platform::web::download_patch(session.current_patch(), extension,
                                    save_as_stem_suggestion(session))) {
    megatoy::status::success("Download started.");
  } else {
    megatoy::status::error("Failed to prepare " + extension + " download.");
  }
}
#endif

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

std::optional<std::string>
download_format_menu(const patches::PatchSession &session) {
  std::optional<std::string> extension;
  if (ImGui::BeginMenu("Download")) {
    for (const auto &format : session.save_formats()) {
      if (ImGui::MenuItem(format.display_name().c_str())) {
        extension = format.extension;
      }
    }
    ImGui::EndMenu();
  }
  return extension;
}

void request_save_to_storage(SaveExportState &state) {
#if defined(MEGATOY_PLATFORM_WEB)
  state.save_to_storage_requested = true;
#else
  (void)state;
#endif
}

void request_save_as(SaveExportState &state) {
  state.save_as_requested = true;
#if defined(MEGATOY_PLATFORM_WEB)
  // Under the button only when the button asked; a shortcut or a menu puts
  // it at the pointer, which is on screen however far the editor is scrolled.
  state.save_as_menu_anchor.reset();
#endif
}

void render_save_export_popups(patches::PatchSession &session,
                               SaveExportState &state) {
  if (state.overwrite_confirmation_pending) {
    ImGui::OpenPopup("Overwrite Confirmation");
    state.overwrite_confirmation_pending = false;
  }
#if defined(MEGATOY_PLATFORM_WEB)
  if (state.save_to_storage_requested) {
    state.save_to_storage_requested = false;
    open_save_to_storage_dialog(session, state);
  }
#endif
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
  render_patch_save_dialog(
      kSaveToStorageTitle, "Save", state.save_as_dialog, session.save_formats(),
      session.repository().workspace(),
      [&session, &state](const std::filesystem::path &folder,
                         const std::string &stem, const std::string &extension,
                         bool overwrite) {
        save_as_into(session, state, folder, stem, extension, overwrite);
      });
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
