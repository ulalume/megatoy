#include "preview.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <type_traits>

#include <imgui_impl_opengl3_loader.h>

#ifndef STB_IMAGE_IMPLEMENTATION_ALREADY_INCLUDED
#define STB_IMAGE_IMPLEMENTATION
#endif
#include <stb_image.h>

#ifdef USE_EMBEDDED_RESOURCES
#include "../../resource_manager.hpp"
#endif

#include "../styles/theme.hpp"

namespace ui {
namespace {
std::filesystem::path assets_base_directory() {
#ifdef VGM_ASSETS_DIR
  static const std::filesystem::path kBase{VGM_ASSETS_DIR};
#else
  static const std::filesystem::path kBase{"assets"};
#endif
  return kBase;
}

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

std::filesystem::path build_asset_path(const std::string &filename) {
  return assets_base_directory() / filename;
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

#ifdef USE_EMBEDDED_RESOURCES
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
      std::cerr << "Failed to load embedded resource: "
                << themed_relative_name
                << '\n';
      entry.failed = true;
      return false;
    }
  }
#endif

  // Fallback to file system loading
  const std::filesystem::path path = build_asset_path(themed_relative_name);
  if (!std::filesystem::exists(path)) {
    std::cerr << "Preview image not found: " << path << '\n';
    entry.failed = true;
    return false;
  }

  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc *data = stbi_load(path.string().c_str(), &width, &height, &channels,
                            STBI_rgb_alpha);
  if (!data) {
    std::cerr << "Failed to load preview image: " << path << " ("
              << stbi_failure_reason() << ")\n";
    entry.failed = true;
    return false;
  }

  GLuint texture_id = 0;
  glGenTextures(1, &texture_id);
  if (!texture_id) {
    std::cerr << "Failed to create OpenGL texture for: " << path << '\n';
    stbi_image_free(data);
    entry.failed = true;
    return false;
  }

  glBindTexture(GL_TEXTURE_2D, texture_id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, data);
  glBindTexture(GL_TEXTURE_2D, 0);

  stbi_image_free(data);

  entry.gl_id = texture_id;
  entry.texture.texture_id = to_imtexture_id<ImTextureID>(texture_id);
  entry.texture.size =
      ImVec2(static_cast<float>(width), static_cast<float>(height));
  entry.loaded = true;

  return true;
}

void PreviewImageList::release_entry(Entry &entry) {
  if (entry.gl_id != 0) {
    GLuint texture_id = entry.gl_id;
    glDeleteTextures(1, &texture_id);
  }
  entry = {};
}

} // namespace ui
