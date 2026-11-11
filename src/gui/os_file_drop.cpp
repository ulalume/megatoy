#include "gui/os_file_drop.hpp"

#include <chrono>
#include <imgui.h>
#include <utility>

namespace ui::os_file_drop {

namespace {

/// Longer than any platform leaves between its reports of a drag held still
/// over the window.
constexpr std::chrono::seconds kQuiet{2};

enum class Phase { Idle, Dragging, LetGo };

Phase g_phase = Phase::Idle;
std::vector<std::filesystem::path> g_paths;
std::chrono::steady_clock::time_point g_last_heard{};

void heard() { g_last_heard = std::chrono::steady_clock::now(); }

} // namespace

void begin() {
  g_phase = Phase::Dragging;
  g_paths.clear();
  heard();
}

void moved() { heard(); }

void add(const std::filesystem::path &path) {
  // A drop onto the app itself, from the Dock, comes with no drag before it.
  if (g_phase == Phase::Idle) {
    g_phase = Phase::Dragging;
  }
  g_paths.push_back(path);
  heard();
}

void complete() { g_phase = g_paths.empty() ? Phase::Idle : Phase::LetGo; }

void submit_source() {
  if (g_phase != Phase::Dragging) {
    // Let go: not carried this frame, so ImGui hands the drag to the place
    // it was held over.
    return;
  }
  if (std::chrono::steady_clock::now() - g_last_heard > kQuiet) {
    g_phase = Phase::Idle;
    g_paths.clear();
    return;
  }
  // Only a folder that takes the files opens under them, as for the tree's
  // own entries.
  if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern |
                                 ImGuiDragDropFlags_SourceNoPreviewTooltip |
                                 ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
    ImGui::SetDragDropPayload(kPayload, nullptr, 0);
    ImGui::EndDragDropSource();
  }
}

std::vector<std::filesystem::path> take() {
  if (g_phase != Phase::LetGo) {
    return {};
  }
  g_phase = Phase::Idle;
  return std::exchange(g_paths, {});
}

} // namespace ui::os_file_drop
