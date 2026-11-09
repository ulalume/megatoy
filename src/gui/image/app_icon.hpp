#pragma once

#include "gui/image/resource_manager.hpp"

#include <imgui.h>

namespace ui {

/// An embedded picture, filtered to be drawn smaller than it is stored; no
/// texture when it could not be loaded.
struct Picture {
  ImTextureID texture{};
  ImVec2 size;
};

inline Picture load_picture(const char *name) {
  Picture picture;
  unsigned int texture = 0;
  int width = 0;
  int height = 0;
  if (ResourceManager::instance().load_texture_from_resource(
          name, texture, width, height, true)) {
    picture.texture = static_cast<ImTextureID>(texture);
    picture.size =
        ImVec2(static_cast<float>(width), static_cast<float>(height));
  }
  return picture;
}

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
