#pragma once

#include "embedded_assets_registry.hpp"
#include <IconsFontAwesome7.h>
#include <imgui.h>
#include <iomanip>
#include <iostream>

namespace ui {
inline void init_icon_font() {

  ImGuiIO &io = ImGui::GetIO();

  if (io.Fonts->Fonts.Size == 0) {
    // Vector rather than the bitmap default: the UI scale re-rasterizes at
    // arbitrary sizes, which ProggyClean does not survive.
    io.Fonts->AddFontDefaultVector(); // ensure merge target exists
  }

  ImFontConfig config;
  config.MergeMode = true;
  config.PixelSnapH = true;
  config.FontDataOwnedByAtlas =
      false; // we keep data in static storage, let ImGui copy it as needed
  static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};

  const auto &res = embedded_assets::resource_registry.at(
      "fonts/Font Awesome 7 Free-Solid-900.otf");

  // Size 0 inherits the destination font's reference size. The default font
  // uses an implicit reference size since ImGui 1.92, and AddFont asserts if a
  // merged font then names an explicit one.
  io.Fonts->AddFontFromMemoryTTF((void *)res.data, (int)res.size, 0.0f, &config,
                                 icon_ranges);
}
} // namespace ui
