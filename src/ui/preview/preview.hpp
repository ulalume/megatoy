#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include <imgui.h>

namespace ui {

struct PreviewTexture {
  ImTextureID texture_id = static_cast<ImTextureID>(0);
  ImVec2 size = ImVec2(0.0f, 0.0f);

  bool valid() const {
    return texture_id != static_cast<ImTextureID>(0) && size.x > 0.0f &&
           size.y > 0.0f;
  }
};

class PreviewImageList {
public:
  explicit PreviewImageList(std::vector<std::string> filenames);
  ~PreviewImageList();

  PreviewImageList(const PreviewImageList &) = delete;
  PreviewImageList &operator=(const PreviewImageList &) = delete;
  PreviewImageList(PreviewImageList &&) = default;
  PreviewImageList &operator=(PreviewImageList &&) = default;

  const PreviewTexture *get(int index);
  void reset();

  size_t size() const { return entries_.size(); }

private:
  struct Entry {
    PreviewTexture texture;
    unsigned int gl_id = 0;
    bool loaded = false;
    bool failed = false;
  };

  bool load_entry(size_t index);
  static void release_entry(Entry &entry);

  std::vector<std::string> filenames_;
  std::vector<Entry> entries_;
};

std::filesystem::path build_asset_path(const std::string &filename);

} // namespace ui
