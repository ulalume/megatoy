#include "envelope_image.hpp"
#include <imgui.h>

namespace ui {

void render_envelope_image(const ym2612::OperatorSettings &op, ImVec2 size) {
  ImGui::BeginChild("EnvelopeImage", size, false, ImGuiWindowFlags_NoScrollbar);

  ImDrawList *draw_list = ImGui::GetWindowDrawList();

  // 描画エリアの座標を取得
  ImVec2 canvas_min = ImGui::GetCursorScreenPos();
  ImVec2 canvas_max = ImVec2(canvas_min.x + size.x, canvas_min.y + size.y);
  ImU32 border_color = ImGui::GetColorU32(ImGuiCol_Separator);
  draw_list->AddRect(canvas_min, canvas_max, border_color);
  ImGui::EndChild();
}

} // namespace ui
