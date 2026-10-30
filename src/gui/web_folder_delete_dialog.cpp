#include "platform/platform_config.hpp"
#include "web_storage_dialogs.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include "gui/components/modal.hpp"
#include "platform/web/web_folder_delete.hpp"
#include <cfloat>
#include <imgui.h>

namespace ui {
namespace {

constexpr const char *kPopupId = "Deleting Folder###FolderDelete";

} // namespace

using namespace platform::web;

void render_folder_delete_ui() {
  const auto *progress = folder_delete_name();
  if (take_folder_delete_finished()) {
    if (ImGui::IsPopupOpen(kPopupId) &&
        ui::begin_modal(kPopupId, ui::ModalDismiss::None).visible) {
      ImGui::CloseCurrentPopup();
      ui::end_modal();
    }
  }

  if (!progress) {
    return;
  }

  const auto &name = *progress;
  // Open on the edge only: re-opening an already-open popup every frame
  // resets popup state and can swallow clicks (see confirmation_dialog).
  if (!ImGui::IsPopupOpen(kPopupId)) {
    ImGui::OpenPopup(kPopupId);
  }
  if (ui::begin_modal(kPopupId, ui::ModalDismiss::None).visible) {
    ImGui::Text("Deleting \"%s\"...", name.c_str());
    ImGui::TextDisabled("Saving the change to browser storage.");
    // Indeterminate: IndexedDB reports no progress, only completion. No
    // Cancel either -- the files are already gone from MEMFS, and stopping
    // the flush would leave browser storage holding the deleted files.
    ImGui::ProgressBar(static_cast<float>(-1.0 * ImGui::GetTime()),
                       ImVec2(-FLT_MIN, 0));
    ui::end_modal();
  }
}

} // namespace ui

#endif
