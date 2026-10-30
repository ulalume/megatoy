#pragma once

#include "preferences/theme.hpp"

namespace ui::styles {

void apply_theme(ThemeId id);
ThemeId current_theme();

/**
 * Rebuild the whole style: base metrics, then theme colours, then scale.
 *
 * ScaleAllSizes multiplies what it finds, so the metrics have to start from
 * their unscaled values every time.
 */
void apply_style(ThemeId id, float scale);

std::string
themed_asset_relative_path(const std::string &filename,
                           const std::string &asset_category = "images");

} // namespace ui::styles
