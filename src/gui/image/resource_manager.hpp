#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

struct EmbeddedResource {
  const uint8_t *data;
  size_t size;
  std::string name;
};

class ResourceManager {
public:
  static ResourceManager &instance();

  // Get embedded resource by name
  const EmbeddedResource *get_resource(const std::string &name) const;
  const EmbeddedResource *get_resource(const std::filesystem::path &path) const;

  // Load texture from embedded resource; see gfx::create_texture_from_rgba
  // for `smooth`.
  bool load_texture_from_resource(const std::string &name,
                                  unsigned int &texture_id, int &width,
                                  int &height, bool smooth = false);

  // Check if resource exists
  bool has_resource(const std::string &name) const;
  bool has_resource(const std::filesystem::path &path) const;

private:
  ResourceManager();
  ~ResourceManager() = default;

  // Delete copy constructor and assignment operator
  ResourceManager(const ResourceManager &) = delete;
  ResourceManager &operator=(const ResourceManager &) = delete;

  void register_embedded_resources();

  std::unordered_map<std::string, EmbeddedResource> resources_;
};
