#include "resource_manager.hpp"

#include <iostream>
#include <vector>

#include <imgui_impl_opengl3_loader.h>

#define STB_IMAGE_IMPLEMENTATION_ALREADY_INCLUDED
#include <stb_image.h>

#ifdef USE_EMBEDDED_RESOURCES
#include "embedded_assets_registry.hpp"
#endif

ResourceManager &ResourceManager::instance() {
  static ResourceManager instance;
  return instance;
}

ResourceManager::ResourceManager() { register_embedded_resources(); }

const EmbeddedResource *
ResourceManager::get_resource(const std::string &name) const {
  auto it = resources_.find(name);
  if (it != resources_.end()) {
    return &it->second;
  }
  return nullptr;
}

bool ResourceManager::load_texture_from_resource(const std::string &name,
                                                 unsigned int &texture_id,
                                                 int &width, int &height) {
  const auto *resource = get_resource(name);
  if (!resource) {
    std::cerr << "Embedded resource not found: " << name << '\n';
    return false;
  }

  int channels = 0;
  stbi_uc *data =
      stbi_load_from_memory(resource->data, static_cast<int>(resource->size),
                            &width, &height, &channels, STBI_rgb_alpha);

  if (!data) {
    std::cerr << "Failed to load embedded image: " << name << " ("
              << stbi_failure_reason() << ")\n";
    return false;
  }

  GLuint gl_texture_id = 0;
  glGenTextures(1, &gl_texture_id);
  if (!gl_texture_id) {
    std::cerr << "Failed to create OpenGL texture for: " << name << '\n';
    stbi_image_free(data);
    return false;
  }

  glBindTexture(GL_TEXTURE_2D, gl_texture_id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, data);
  glBindTexture(GL_TEXTURE_2D, 0);

  stbi_image_free(data);
  texture_id = gl_texture_id;
  return true;
}

bool ResourceManager::has_resource(const std::string &name) const {
  return resources_.find(name) != resources_.end();
}

std::vector<std::string> ResourceManager::get_resource_names() const {
  std::vector<std::string> names;
  names.reserve(resources_.size());
  for (const auto &pair : resources_) {
    names.push_back(pair.first);
  }
  return names;
}

void ResourceManager::register_embedded_resources() {
#ifdef USE_EMBEDDED_RESOURCES
  // Register all embedded resources from the generated registry
  for (const auto &entry : embedded_assets::resource_registry) {
    EmbeddedResource resource;
    resource.data = entry.second.data;
    resource.size = entry.second.size;
    resource.name = entry.first;
    resources_[entry.first] = resource;
  }

  std::cout << "Registered " << resources_.size() << " embedded resources\n";
#else
  std::cout << "Embedded resources not available (USE_EMBEDDED_RESOURCES not "
               "defined)\n";
#endif
}
