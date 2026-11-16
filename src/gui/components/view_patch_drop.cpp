#include "view_patch_drop.hpp"

#include "patch_selector.hpp"
#include "sharp_frame.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <utility>

namespace ui {
namespace {
std::optional<ViewPatchDrop> pending_drop;
} // namespace

void view_patch_drop_target() {
  ImGuiWindow *window = ImGui::GetCurrentWindow();
  // A tab behind another one in its node shares the node's rect.
  if (window->SkipItems) {
    return;
  }
  if (!ImGui::BeginDragDropTargetCustom(window->Rect(),
                                        window->GetID("##view_patch_drop"))) {
    return;
  }
  // ImGui's own frame is clipped to the contents and then grown, which
  // misses the view's edges; this one runs along them, from under the tabs
  // or title bar to the far side of the scrollbar.
  const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(
      kPatchEntryPayload, ImGuiDragDropFlags_AcceptBeforeDelivery |
                              ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
  if (payload != nullptr && payload->IsPreview()) {
    const ImVec2 min(window->Pos.x, window->InnerRect.Min.y);
    const ImVec2 max(window->Pos.x + window->Size.x,
                     window->Pos.y + window->Size.y);
    draw_sharp_frame(ImGui::GetForegroundDrawList(window->Viewport), min, max,
                     ImGui::GetColorU32(ImGuiCol_DragDropTarget),
                     ImGui::GetStyle().DragDropTargetBorderSize);
  }
  if (payload != nullptr && payload->IsDelivery()) {
    pending_drop = ViewPatchDrop{
        std::string(static_cast<const char *>(payload->Data),
                    static_cast<std::size_t>(payload->DataSize)),
        window->DockId};
  }
  ImGui::EndDragDropTarget();
}

std::optional<ViewPatchDrop> take_view_patch_drop() {
  return std::exchange(pending_drop, std::nullopt);
}

} // namespace ui
