#include "platform/platform_config.hpp"
#include "web_storage_dialogs.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include "gui/components/modal.hpp"
#include "platform/web/web_storage_flush.hpp"
#include <cfloat>
#include <imgui.h>

namespace ui {
namespace {

constexpr const char *kPopupId = "Saving###AwaitedStorageFlush";

} // namespace

using namespace platform::web;

void render_awaited_flush_ui() {
  const auto *progress = awaited_flush_progress();
  if (take_awaited_flush_finished()) {
    if (ImGui::IsPopupOpen(kPopupId) &&
        ui::begin_modal(kPopupId, ui::ModalDismiss::None).visible) {
      ImGui::CloseCurrentPopup();
      ui::end_modal();
    }
  }

  if (!progress) {
    return;
  }

  const auto &active = *progress;
  // Open on the edge only: re-opening an already-open popup every frame
  // resets popup state and can swallow clicks.
  if (!ImGui::IsPopupOpen(kPopupId)) {
    ImGui::OpenPopup(kPopupId);
  }
  if (ui::begin_modal(kPopupId, ui::ModalDismiss::None).visible) {
    ImGui::TextUnformatted(active.title.c_str());
    ImGui::TextDisabled("%s", active.detail.c_str());
    // Indeterminate, and no Cancel: IndexedDB reports only completion, and
    // the first half of a flush never gives the frame back.
    ImGui::ProgressBar(static_cast<float>(-1.0 * ImGui::GetTime()),
                       ImVec2(-FLT_MIN, 0));
    ui::end_modal();
  }
}

} // namespace ui

#endif
