#include <imgui.h>
namespace ui::styles::megatoy_light {
inline void apply() {
  ImGui::StyleColorsLight();
  ImVec4 *colors = ImGui::GetStyle().Colors;

  colors[ImGuiCol_Text] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
  colors[ImGuiCol_TextDisabled] = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
  colors[ImGuiCol_WindowBg] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
  colors[ImGuiCol_ChildBg] = ImVec4(0.95f, 0.95f, 0.95f, 0.00f);
  colors[ImGuiCol_PopupBg] = ImVec4(0.95f, 0.95f, 0.95f, 0.90f);
  colors[ImGuiCol_Border] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
  colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_FrameBg] = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
  colors[ImGuiCol_FrameBgHovered] = ImVec4(0.45f, 0.65f, 0.95f, 0.78f);
  colors[ImGuiCol_FrameBgActive] = ImVec4(0.45f, 0.65f, 0.95f, 1.00f);

  colors[ImGuiCol_TitleBg] = ImVec4(0.20f, 0.40f, 0.70f, 1.00f);
  colors[ImGuiCol_TitleBgActive] = ImVec4(0.25f, 0.45f, 0.75f, 1.00f);
  colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.15f, 0.30f, 0.55f, 0.75f);
  colors[ImGuiCol_MenuBarBg] = ImVec4(0.20f, 0.40f, 0.70f, 0.80f);

  colors[ImGuiCol_ScrollbarBg] = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
  colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
  colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.45f, 0.65f, 0.95f, 0.78f);
  colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.45f, 0.65f, 0.95f, 1.00f);

  colors[ImGuiCol_CheckMark] = ImVec4(0.10f, 0.30f, 0.60f, 1.00f);
  colors[ImGuiCol_SliderGrab] = ImVec4(0.45f, 0.65f, 0.95f, 0.39f);
  colors[ImGuiCol_SliderGrabActive] = ImVec4(0.45f, 0.65f, 0.95f, 0.78f);

  colors[ImGuiCol_Button] = ImVec4(0.45f, 0.75f, 0.85f, 0.80f);
  colors[ImGuiCol_ButtonHovered] = ImVec4(0.45f, 0.65f, 0.95f, 0.90f);
  colors[ImGuiCol_ButtonActive] = ImVec4(0.45f, 0.65f, 0.95f, 1.00f);

  colors[ImGuiCol_Header] = ImVec4(0.45f, 0.65f, 0.95f, 0.41f);
  colors[ImGuiCol_HeaderHovered] = ImVec4(0.45f, 0.65f, 0.95f, 0.86f);
  colors[ImGuiCol_HeaderActive] = ImVec4(0.45f, 0.65f, 0.95f, 1.00f);

  colors[ImGuiCol_Separator] = ImVec4(0.70f, 0.70f, 0.70f, 0.78f);
  colors[ImGuiCol_SeparatorHovered] = ImVec4(0.45f, 0.65f, 0.95f, 0.78f);
  colors[ImGuiCol_SeparatorActive] = ImVec4(0.45f, 0.65f, 0.95f, 1.00f);

  colors[ImGuiCol_ResizeGrip] = ImVec4(0.45f, 0.65f, 0.95f, 1.00f);
  colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.45f, 0.65f, 0.95f, 0.78f);
  colors[ImGuiCol_ResizeGripActive] = ImVec4(0.45f, 0.65f, 0.95f, 1.00f);

  colors[ImGuiCol_InputTextCursor] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);

  colors[ImGuiCol_TabHovered] = ImVec4(0.45f, 0.65f, 0.95f, 0.78f);
  colors[ImGuiCol_Tab] = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
  colors[ImGuiCol_TabSelected] = ImVec4(0.45f, 0.65f, 0.95f, 0.41f);
  colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.45f, 0.65f, 0.95f, 0.41f);
  colors[ImGuiCol_TabDimmed] = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
  colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
  colors[ImGuiCol_TabDimmedSelectedOverline] =
      ImVec4(0.50f, 0.50f, 0.50f, 0.00f);

  colors[ImGuiCol_DockingPreview] = ImVec4(0.45f, 0.65f, 0.95f, 1.00f);
  colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);

  colors[ImGuiCol_PlotLines] = ImVec4(0.00f, 0.00f, 0.00f, 0.63f);
  colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.45f, 0.65f, 0.95f, 1.00f);
  colors[ImGuiCol_PlotHistogram] = ImVec4(0.00f, 0.00f, 0.00f, 0.63f);
  colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.45f, 0.65f, 0.95f, 1.00f);

  colors[ImGuiCol_TableHeaderBg] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
  colors[ImGuiCol_TableBorderStrong] = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
  colors[ImGuiCol_TableBorderLight] = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
  colors[ImGuiCol_TableRowBg] = ImVec4(0.90f, 0.90f, 0.90f, 0.00f);
  colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.00f, 0.00f, 0.00f, 0.06f);

  colors[ImGuiCol_TextLink] = ImVec4(0.00f, 0.00f, 1.00f, 1.00f);
  colors[ImGuiCol_TextSelectedBg] = ImVec4(0.45f, 0.65f, 0.95f, 0.43f);

  colors[ImGuiCol_TreeLines] = ImVec4(0.40f, 0.40f, 0.40f, 0.50f);
  colors[ImGuiCol_DragDropTarget] = ImVec4(0.00f, 0.00f, 0.00f, 0.90f);
  colors[ImGuiCol_NavCursor] = ImVec4(0.45f, 0.65f, 0.95f, 1.00f);
  colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.00f, 0.00f, 0.00f, 0.70f);
  colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.20f);
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.90f, 0.90f, 0.90f, 0.73f);
}

} // namespace ui::styles::megatoy_light
