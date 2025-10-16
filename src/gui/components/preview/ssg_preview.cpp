#include "ssg_preview.hpp"

#include <string>
#include <vector>

namespace ui {
namespace {
std::vector<std::string> build_filenames() {
  std::vector<std::string> names;
  names.reserve(9);
  for (int i = 0; i < 8; ++i) {
    names.emplace_back("ssg" + std::to_string(i) + ".png");
  }
  names.emplace_back("ssgoff.png");
  return names;
}

PreviewImageList g_ssg_previews(build_filenames());
constexpr int kOffIndex = 8;

} // namespace

const PreviewTexture *get_ssg_preview_texture(int envelope_type) {
  return g_ssg_previews.get(envelope_type);
}

const PreviewTexture *get_ssg_preview_off_texture() {
  return g_ssg_previews.get(kOffIndex);
}

void reset_ssg_preview_textures() { g_ssg_previews.reset(); }

} // namespace ui
