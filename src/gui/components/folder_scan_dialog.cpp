#include "folder_scan_dialog.hpp"

#if !defined(MEGATOY_PLATFORM_WEB)

#include "common.hpp"
#include "modal.hpp"
#include "patches/background_folder_scan.hpp"

#include <cfloat>
#include <chrono>
#include <imgui.h>

namespace ui {
namespace {

constexpr const char *kScanPopupTitle = "Scanning Folder";
constexpr auto kGracePeriod = std::chrono::milliseconds(150);
constexpr ImGuiWindowFlags kScanPopupFlags =
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;

std::chrono::steady_clock::time_point g_scan_started_at{};

void close_scan_popup() {
  if (ImGui::IsPopupOpen(kScanPopupTitle) &&
      ImGui::BeginPopupModal(kScanPopupTitle, nullptr, kScanPopupFlags)) {
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

} // namespace

void render_folder_scan_dialog() {
  namespace scan = patches::background_folder_scan;

  scan::poll_completion();

  const auto status = scan::status();
  if (!status.active) {
    g_scan_started_at = {};
    close_scan_popup();
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (g_scan_started_at == std::chrono::steady_clock::time_point{}) {
    g_scan_started_at = now;
  }
  // A folder of a dozen files is done in a frame or two, and a dialog that
  // flashes for that long reads as a glitch rather than as progress.
  if (now - g_scan_started_at < kGracePeriod) {
    return;
  }

  // Open on the edge only: re-opening an already-open popup every frame
  // resets popup state and can swallow clicks (see confirmation_dialog).
  if (!ImGui::IsPopupOpen(kScanPopupTitle)) {
    ImGui::OpenPopup(kScanPopupTitle);
  }
  // No way out but the button: Cancel stops the scan, and a dismissal that
  // only hid the dialog would leave it running with nothing to stop it.
  if (begin_modal(kScanPopupTitle, ModalDismiss::None).visible) {
    ImGui::Text("Scanning \"%s\"... %zu files", status.folder_name.c_str(),
                status.files_seen);
    if (status.containers_parsed > 0) {
      ImGui::TextDisabled("%zu banks parsed", status.containers_parsed);
    }
    // Nothing knows the file count up front, so the bar animates rather than
    // fills: a negative fraction is ImGui's indeterminate mode.
    ImGui::ProgressBar(static_cast<float>(-ImGui::GetTime()),
                       ImVec2(-FLT_MIN, 0));
    ImGui::Spacing();
    align_buttons_right({dialog_button_width()});
    if (ImGui::Button("Cancel", ImVec2(dialog_button_width(), 0))) {
      scan::request_cancel();
    }
    end_modal();
  }
}

} // namespace ui

#endif
