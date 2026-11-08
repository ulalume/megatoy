#pragma once

#include "embedded_assets_registry.hpp"
#include "megatoy_icons.hpp"
#include <imgui.h>

namespace ui {
/// Merges megatoy's icon font, every icon it draws, into the default font.
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

  // Size 0 inherits the destination font's reference size. The default font
  // uses an implicit reference size since ImGui 1.92, and AddFont asserts if a
  // merged font then names an explicit one.
  static const ImWchar ranges[] = {ICON_MIN_MEGATOY, ICON_MAX_MEGATOY,
                                   ICON_MIN_MEGATOY_INSTRUMENT,
                                   ICON_MAX_MEGATOY_INSTRUMENT, 0};
  const auto &font =
      embedded_assets::resource_registry.at("fonts/megatoy-icons.otf");
  io.Fonts->AddFontFromMemoryTTF((void *)font.data, (int)font.size, 0.0f,
                                 &config, ranges);
}
} // namespace ui
