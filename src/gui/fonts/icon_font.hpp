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
    io.Fonts->AddFontDefault(); // ensure merge target exists
  }

  ImFontConfig config;
  config.MergeMode = true;
  config.PixelSnapH = true;
  config.FontDataOwnedByAtlas =
      false; // we keep data in static storage, let ImGui copy it as needed
  static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};

  const auto &res = embedded_assets::resource_registry.at(
      "fonts/Font Awesome 7 Free-Solid-900.otf");

  io.Fonts->AddFontFromMemoryTTF((void *)res.data, (int)res.size, 10.0f,
                                 &config, icon_ranges);
}
} // namespace ui
