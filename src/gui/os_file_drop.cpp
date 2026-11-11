#include "gui/os_file_drop.hpp"

#include <imgui.h>
#include <utility>

namespace ui::os_file_drop {

namespace {

enum class Phase {
  Idle,
  /// Being dragged over the window.
  Dragging,
  /// Let go: the targets take them on the next frame.
  Dropped,
  /// That frame is drawn: what no target took goes where a drop always did.
  Unclaimed,
};

Phase g_phase = Phase::Idle;
std::vector<std::filesystem::path> g_paths;

} // namespace

void begin() {
  g_phase = Phase::Dragging;
  g_paths.clear();
}

void add(const std::filesystem::path &path) {
  // A drop onto the app itself, from the Dock, comes with no drag before it.
  if (g_phase == Phase::Idle) {
    g_phase = Phase::Dragging;
  }
  g_paths.push_back(path);
}

void complete() { g_phase = g_paths.empty() ? Phase::Idle : Phase::Dropped; }

void cancel() {
  g_phase = Phase::Idle;
  g_paths.clear();
}

void submit_source() {
  if (g_phase == Phase::Dropped) {
    // Not carried this frame: ImGui then hands the drag to the target it was
    // held over. What none takes is left for take_unclaimed().
    g_phase = Phase::Unclaimed;
    return;
  }
  if (g_phase != Phase::Dragging) {
    return;
  }
  if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern |
                                 ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
    ImGui::SetDragDropPayload(kPayload, nullptr, 0);
    ImGui::EndDragDropSource();
  }
}

std::vector<std::filesystem::path> take() {
  if (g_phase != Phase::Unclaimed) {
    return {};
  }
  g_phase = Phase::Idle;
  return std::exchange(g_paths, {});
}

std::vector<std::filesystem::path> take_unclaimed() { return take(); }

} // namespace ui::os_file_drop
