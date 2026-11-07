#include "platform/platform_config.hpp"
#include "web_storage_dialogs.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include "core/status.hpp"
#include "gui/components/common.hpp"
#include "platform/web/web_folder_import.hpp"
#include <imgui.h>
#include <sstream>

namespace ui {
namespace {

constexpr const char *kSelectionPopupId = "Import Folder###ImportSelection";

std::string format_megabytes(std::uint64_t bytes) {
  std::ostringstream stream;
  stream.setf(std::ios::fixed);
  stream.precision(bytes >= 10 * 1024 * 1024 ? 1 : 2);
  stream << static_cast<double>(bytes) / (1024.0 * 1024.0);
  return stream.str();
}

} // namespace

using namespace platform::web;

void render_folder_import_ui() {
  advance_folder_import();

  if (take_folder_import_finished()) {
    const char *title = ImGui::IsPopupOpen("Importing Folder")
                            ? "Importing Folder"
                            : "Import Folder";
    if (ImGui::IsPopupOpen(title) &&
        ImGui::BeginPopupModal(title, nullptr,
                               ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
  }

  if (take_folder_selection_finished()) {
    if (ImGui::IsPopupOpen(kSelectionPopupId) &&
        ImGui::BeginPopupModal(kSelectionPopupId, nullptr,
                               ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
  }

  // The active-import modals take over the moment the selection resolves; by
  // construction the two never coexist, but the guard keeps it that way. A
  // hidden slot (cancelled while the browser owned the flow) renders nothing.
  if (const auto progress = folder_selection_progress()) {
    const auto &selection = *progress;
    // Open on the edge only, as with the import modals below.
    if (!ImGui::IsPopupOpen(kSelectionPopupId)) {
      ImGui::OpenPopup(kSelectionPopupId);
    }
    ui::center_next_window();
    if (ImGui::BeginPopupModal(kSelectionPopupId, nullptr,
                               ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
      ui::force_center_window();
      const auto phase = selection.phase;
      if (phase == SelectionPhase::WaitingForPicker) {
        ImGui::TextUnformatted("Choose a folder to import.");
        ImGui::TextDisabled("Large folders can take the browser a while to "
                            "read after you choose.");
        ImGui::TextDisabled(
            "You can close this dialog; the import will continue.");
        ImGui::ProgressBar(static_cast<float>(-1.0 * ImGui::GetTime()),
                           ImVec2(360, 0));
      } else {
        const auto scanned = static_cast<unsigned long long>(selection.scanned);
        const auto total = static_cast<unsigned long long>(selection.total);
        if (total == 0) {
          if (selection.is_drop) {
            ImGui::Text("Scanning dropped folder: %llu files...", scanned);
          } else {
            ImGui::Text("Scanning selection: %llu files...", scanned);
          }
          ImGui::ProgressBar(static_cast<float>(-1.0 * ImGui::GetTime()),
                             ImVec2(360, 0));
        } else {
          ImGui::Text("Scanning selection: %llu / %llu files...", scanned,
                      total);
          ImGui::ProgressBar(std::clamp(static_cast<float>(scanned) /
                                            static_cast<float>(total),
                                        0.0f, 1.0f),
                             ImVec2(360, 0));
        }
      }
      ImGui::Spacing();
      if (phase == SelectionPhase::WaitingForPicker) {
        // The browser owns the chooser flow and cannot be stopped from here,
        // so this button only dismisses the dialog. The slot stays held --
        // hidden -- which keeps a second picker from being queued behind the
        // pending one, and the dialog returns when the files arrive. To
        // abort, the user cancels the browser's own dialog, whose cancel
        // event frees the slot.
        if (ImGui::Button("Close", ImVec2(120, 0))) {
          ImGui::CloseCurrentPopup();
          dismiss_folder_selection();
        }
      } else if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        // The files are already ours; destroying the slot is enough. Any
        // JavaScript still holding the token sees an unknown token, reads
        // it as abandoned and unwinds silently.
        ImGui::CloseCurrentPopup();
        cancel_folder_selection();
      }
      ImGui::EndPopup();
    }
  }

  if (const auto progress = folder_import_progress()) {
    const auto &request = *progress;
    const auto phase = request.phase;
    if (phase == ImportPhase::AwaitingConfirmation) {
      // Open on the edge only: re-opening an already-open popup every frame
      // resets popup state and can swallow clicks (see confirmation_dialog).
      if (!ImGui::IsPopupOpen("Import Folder")) {
        ImGui::OpenPopup("Import Folder");
      }
      ui::center_next_window();
      if (ImGui::BeginPopupModal("Import Folder", nullptr,
                                 ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_AlwaysAutoResize)) {
        ui::force_center_window();
        ImGui::TextWrapped("Import %zu files (%s MB) from \"%s\"?",
                           request.kept_count,
                           format_megabytes(request.kept_bytes).c_str(),
                           request.folder_name.c_str());
        if (request.filtered_count > 0) {
          ImGui::TextDisabled("%zu unrelated files skipped",
                              request.filtered_count);
        }
        if (request.quota_remaining >= 0) {
          ImGui::TextDisabled("Browser storage remaining: %s MB",
                              format_megabytes(static_cast<std::uint64_t>(
                                                   request.quota_remaining))
                                  .c_str());
        }
        ImGui::Spacing();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
          ImGui::CloseCurrentPopup();
          decide_folder_import(false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Import", ImVec2(120, 0))) {
          decide_folder_import(true);
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
    } else {
      if (!ImGui::IsPopupOpen("Importing Folder")) {
        ImGui::OpenPopup("Importing Folder");
      }
      ui::center_next_window();
      if (ImGui::BeginPopupModal("Importing Folder", nullptr,
                                 ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_AlwaysAutoResize)) {
        ui::force_center_window();
        const auto current = request.current_files;
        const auto total = request.total_files;
        const char *label = phase == ImportPhase::Copying      ? "Copying"
                            : phase == ImportPhase::Validating ? "Validating"
                                                               : "Saving";
        if (request.cancel_requested) {
          ImGui::TextUnformatted("Cancelling...");
        } else {
          ImGui::Text("%s %zu/%zu...", label, current, total);
        }
        float fraction = total == 0 ? 0.0f
                                    : static_cast<float>(current) /
                                          static_cast<float>(total);
        if (phase == ImportPhase::Copying) {
          const auto current_bytes = request.current_bytes;
          const auto total_bytes = request.total_bytes;
          if (total_bytes > 0) {
            fraction = static_cast<float>(current_bytes) /
                       static_cast<float>(total_bytes);
          }
        }
        ImGui::ProgressBar(std::clamp(fraction, 0.0f, 1.0f), ImVec2(360, 0));
        ImGui::Spacing();
        ImGui::BeginDisabled(request.cancel_requested);
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
          cancel_folder_import();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
      }
    }
  }
}

} // namespace ui

#endif
