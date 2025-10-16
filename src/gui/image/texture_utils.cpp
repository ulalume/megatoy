#include "texture_utils.hpp"

#include <iostream>

namespace gfx {

bool create_texture_from_rgba(const uint8_t *pixels, int width, int height,
                              GLuint &texture_id) {
  if (pixels == nullptr || width <= 0 || height <= 0) {
    std::cerr << "Invalid texture data parameters" << '\n';
    return false;
  }

  GLuint gl_texture_id = 0;
  glGenTextures(1, &gl_texture_id);
  if (gl_texture_id == 0) {
    std::cerr << "Failed to generate OpenGL texture" << '\n';
    return false;
  }

  glBindTexture(GL_TEXTURE_2D, gl_texture_id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, pixels);
  glBindTexture(GL_TEXTURE_2D, 0);

  texture_id = gl_texture_id;
  return true;
}

} // namespace gfx
