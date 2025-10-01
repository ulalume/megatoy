#include "resource_manager.hpp"

#include <filesystem>
#include <iostream>
#include <stb_image.h>
#include <vector>

#include "embedded_assets_registry.hpp"
#include "graphics/texture_utils.hpp"

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

const EmbeddedResource *
ResourceManager::get_resource(const std::filesystem::path &path) const {
  return get_resource(path.generic_string());
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
  if (!gfx::create_texture_from_rgba(data, width, height, gl_texture_id)) {
    stbi_image_free(data);
    std::cerr << "Failed to create OpenGL texture for: " << name << '\n';
    return false;
  }

  stbi_image_free(data);
  texture_id = gl_texture_id;
  return true;
}

bool ResourceManager::has_resource(const std::string &name) const {
  return resources_.find(name) != resources_.end();
}

bool ResourceManager::has_resource(const std::filesystem::path &path) const {
  return has_resource(path.generic_string());
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
  // Register all embedded resources from the generated registry
  for (const auto &entry : embedded_assets::resource_registry) {
    EmbeddedResource resource;
    resource.data = entry.second.data;
    resource.size = entry.second.size;
    resource.name = entry.first;
    resources_[entry.first] = resource;
  }

  std::cout << "Registered " << resources_.size() << " embedded resources\n";
}
