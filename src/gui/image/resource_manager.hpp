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

  const EmbeddedResource *get_resource(const std::string &name) const;
  const EmbeddedResource *get_resource(const std::filesystem::path &path) const;

  // See gfx::create_texture_from_rgba for `smooth`.
  bool load_texture_from_resource(const std::string &name,
                                  unsigned int &texture_id, int &width,
                                  int &height, bool smooth = false);

  bool has_resource(const std::string &name) const;
  bool has_resource(const std::filesystem::path &path) const;

private:
  ResourceManager();
  ~ResourceManager() = default;

  ResourceManager(const ResourceManager &) = delete;
  ResourceManager &operator=(const ResourceManager &) = delete;

  void register_embedded_resources();

  std::unordered_map<std::string, EmbeddedResource> resources_;
};
