#include "algorithm_preview.hpp"

#include <string>
#include <vector>

namespace ui {
namespace {
std::vector<std::string> build_filenames() {
  std::vector<std::string> names;
  names.reserve(8);
  for (int i = 0; i < 8; ++i) {
    names.emplace_back("algorithm" + std::to_string(i) + ".png");
  }
  return names;
}

PreviewImageList g_algorithm_previews(build_filenames());

} // namespace

const PreviewTexture *get_algorithm_preview_texture(int algorithm) {
  return g_algorithm_previews.get(algorithm);
}

void reset_algorithm_preview_textures() { g_algorithm_previews.reset(); }

} // namespace ui
