#include "gui/ui_scale.hpp"

#include <algorithm>

namespace ui::scale {

float resolve(float preference, float display_scale) {
  const float factor = preference > 0.0f ? preference : display_scale;
  if (!(factor > 0.0f)) {
    return kMin;
  }
  return std::clamp(factor, kMin, kMax);
}

} // namespace ui::scale
