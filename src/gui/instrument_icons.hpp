#pragma once

#include <span>
#include <string_view>

namespace ui::instrument_icons {

/// A patch's instrument icon: the id the folder's metadata keeps, and its
/// glyph in megatoy's icon font as UTF-8.
struct Icon {
  std::string_view id;
  const char *glyph;
};

struct Group {
  std::string_view name;
  std::span<const Icon> icons;
};

/// Every icon, by group, in the order the picker shows them.
std::span<const Group> groups();

/// The glyph shown for a patch without an icon.
const char *none_glyph();

/// The glyph of `id`, or the none glyph for an empty or unknown id.
const char *glyph(std::string_view id);

/// Whether `id` holds `filter`, ignoring case. An empty filter matches every
/// id.
bool matches(std::string_view id, std::string_view filter);

} // namespace ui::instrument_icons
