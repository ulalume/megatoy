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
                               SaveExportState &state,
                               UIState::TextPromptState &text_prompt_state) {
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
#if defined(MEGATOY_PLATFORM_WEB)
    const std::string extension = *selected_extension;
    text_prompt_state.request(
        "Save Patch As", "Filename", save_as_stem_suggestion(session), "Save",
        [&session, &state, extension](const std::string &stem) {
          auto result = session.save_current_patch_as(extension, stem);
          if (result.is_duplicated()) {
            state.pending_save_as_extension = extension;
            state.pending_save_as_stem = stem;
            state.overwrite_confirmation_pending = true;
          } else {
            announce_save(session, result);
          }
        },
        [](const std::string &stem) {
          if (stem.empty()) {
            return std::string("Filename cannot be empty.");
          }
          if (patches::sanitize_filename(stem) != stem) {
            return std::string("Filename contains invalid characters.");
          }
          return std::string{};
        });
#else
    auto result = session.save_current_patch_as(*selected_extension);
    if (result.is_duplicated()) {
      state.pending_save_as_extension = *selected_extension;
      state.overwrite_confirmation_pending = true;
    } else {
      announce_save(session, result);
    }
#endif
  }

  // Escape cancels: overwriting destroys the file that is already there, so
  // it only happens when it is asked for.
  auto overwrite = begin_modal("Overwrite Confirmation", ModalDismiss::Escape);
  if (overwrite.dismissed) {
    state.pending_save_as_extension.reset();
    state.pending_save_as_stem.reset();
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

    const bool cancel_button = ImGui::Button("Cancel", ImVec2(120, 0));
    if (cancel_button) {
      state.pending_save_as_extension.reset();
      state.pending_save_as_stem.reset();
      ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    const bool overwrite_button = ImGui::Button("Overwrite", ImVec2(120, 0));
    if (overwrite_button) {
      ImGui::CloseCurrentPopup();
    }

    end_modal();

    if (overwrite_button) {
      const auto pending_extension = state.pending_save_as_extension;
      const auto pending_stem = state.pending_save_as_stem;
      state.pending_save_as_extension.reset();
      state.pending_save_as_stem.reset();
      auto result = pending_extension
                        ? session.save_current_patch_as_forced(
                              *pending_extension, pending_stem.value_or(""))
                        : session.save_current_patch();
      if (result.is_success()) {
        session.set_current_patch_path(
            session.repository().to_relative_path(result.path));
      }
      announce_save(session, result);
    }
  }
}

} // namespace ui
