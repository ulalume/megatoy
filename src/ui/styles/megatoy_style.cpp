#include "megatoy_style.hpp"

#include <imgui.h>

namespace ui::styles {
namespace {

MegatoyStyle g_style{};

} // namespace

MegatoyStyle &mutable_style() { return g_style; }

const MegatoyStyle &style() { return g_style; }

const ImVec4 &color(MegatoyCol col) {
  return g_style.colors[static_cast<int>(col)];
}

ImU32 color_u32(MegatoyCol col) {
  return ImGui::ColorConvertFloat4ToU32(color(col));
}

} // namespace ui::styles
