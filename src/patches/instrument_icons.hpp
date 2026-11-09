#pragma once

#include <span>
#include <string>
#include <string_view>

namespace patches::instrument_icons {

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

/// The icon `id` names, or none for an empty or unknown id.
const Icon *find(std::string_view id);

/// The glyph of `id`, or the none glyph for an empty or unknown id.
const char *glyph(std::string_view id);

/// Whether `id` names one of the icons.
bool known(std::string_view id);

/// What `id` is called on screen, such as "Grand Piano" for "grand-piano".
std::string display_name(std::string_view id);

/// The name of the group holding `id`, or empty for an unknown id.
std::string_view group_name(std::string_view id);

/**
 * Whether `filter` is found in the id, its name or its group's name,
 * ignoring case, and with a space and a hyphen the same. An empty filter
 * matches every id.
 */
bool matches(std::string_view id, std::string_view filter);

} // namespace patches::instrument_icons
