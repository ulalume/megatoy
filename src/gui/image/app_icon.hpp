#pragma once

#include "gui/image/resource_manager.hpp"

#include <imgui.h>

namespace ui {

/// The app's icon, loaded once; 0 when it could not be.
inline ImTextureID app_icon() {
  static const ImTextureID icon = [] {
    unsigned int texture = 0;
    int width = 0;
    int height = 0;
    if (!ResourceManager::instance().load_texture_from_resource(
            "images/app_icon.png", texture, width, height)) {
      return ImTextureID{};
    }
    return static_cast<ImTextureID>(texture);
  }();
  return icon;
}

} // namespace ui
