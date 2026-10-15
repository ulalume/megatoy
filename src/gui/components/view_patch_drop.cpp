#include "view_patch_drop.hpp"

#include "patch_selector.hpp"
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
    // Four strips rather than a stroked rectangle, which is antialiased and
    // so bleeds a pixel either side of the edge.
    const ImVec2 min(window->Pos.x, window->InnerRect.Min.y);
    const ImVec2 max(window->Pos.x + window->Size.x,
                     window->Pos.y + window->Size.y);
    const float t = ImGui::GetStyle().DragDropTargetBorderSize;
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
    ImDrawList *draw_list = ImGui::GetForegroundDrawList(window->Viewport);
    draw_list->AddRectFilled(min, ImVec2(max.x, min.y + t), color);
    draw_list->AddRectFilled(ImVec2(min.x, max.y - t), max, color);
    draw_list->AddRectFilled(ImVec2(min.x, min.y + t),
                             ImVec2(min.x + t, max.y - t), color);
    draw_list->AddRectFilled(ImVec2(max.x - t, min.y + t),
                             ImVec2(max.x, max.y - t), color);
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
