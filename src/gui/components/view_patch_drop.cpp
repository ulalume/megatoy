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
  if (const ImGuiPayload *payload =
          ImGui::AcceptDragDropPayload(kPatchEntryPayload)) {
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
