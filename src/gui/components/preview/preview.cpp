#include "preview.hpp"

#include "gui/image/resource_manager.hpp"
#include "gui/styles/theme.hpp"
#include "platform/platform_config.hpp"
#include <cstdint>
#if defined(MEGATOY_PLATFORM_WEB)
#include <GLES3/gl3.h>
#else
#include <imgui_impl_opengl3_loader.h>
#endif
#include <iostream>
#include <string>
#include <type_traits>

namespace ui {
namespace {

template <typename TextureId>
std::enable_if_t<std::is_pointer_v<TextureId>, TextureId>
to_imtexture_id(GLuint texture_id) {
  return reinterpret_cast<TextureId>(static_cast<uintptr_t>(texture_id));
}

template <typename TextureId>
std::enable_if_t<!std::is_pointer_v<TextureId>, TextureId>
to_imtexture_id(GLuint texture_id) {
  return static_cast<TextureId>(texture_id);
}
} // namespace

PreviewImageList::PreviewImageList(std::vector<std::string> filenames)
    : filenames_(std::move(filenames)), entries_(filenames_.size()) {}

PreviewImageList::~PreviewImageList() = default;

const PreviewTexture *PreviewImageList::get(int index) {
  if (index < 0 || index >= static_cast<int>(entries_.size())) {
    return nullptr;
  }
  if (!load_entry(index)) {
    return nullptr;
  }
  return &entries_[index].texture;
}

void PreviewImageList::reset() {
  for (auto &entry : entries_) {
    release_entry(entry);
  }
}

bool PreviewImageList::load_entry(size_t index) {
  if (index >= entries_.size()) {
    return false;
  }

  Entry &entry = entries_[index];
  if (entry.loaded) {
    return true;
  }
  if (entry.failed) {
    return false;
  }

  const std::string &relative_name = filenames_[index];
  const std::string themed_relative_name =
      styles::themed_asset_relative_path(relative_name);

  // Try to load from embedded resources first
  auto &rm = ResourceManager::instance();
  if (rm.has_resource(themed_relative_name)) {
    unsigned int texture_id = 0;
    int width = 0;
    int height = 0;

    if (rm.load_texture_from_resource(themed_relative_name, texture_id, width,
                                      height)) {
      entry.gl_id = texture_id;
      entry.texture.texture_id = to_imtexture_id<ImTextureID>(texture_id);
      entry.texture.size =
          ImVec2(static_cast<float>(width), static_cast<float>(height));
      entry.loaded = true;
      return true;
    } else {
      std::cerr << "Failed to load embedded resource: " << themed_relative_name
                << '\n';
      entry.failed = true;
      return false;
    }
  }

  std::cerr << "Embedded preview resource missing: " << themed_relative_name
            << '\n';
  entry.failed = true;
  return false;
}

void PreviewImageList::release_entry(Entry &entry) {
  if (entry.gl_id != 0) {
    GLuint texture_id = entry.gl_id;
    glDeleteTextures(1, &texture_id);
  }
  entry = {};
}

} // namespace ui
