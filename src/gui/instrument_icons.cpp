#include "instrument_icons.hpp"

#include <algorithm>
#include <cctype>

namespace ui::instrument_icons {

namespace {
#include "gui/fonts/instrument_icon_table.inc"

char lower(char c) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}
} // namespace

std::span<const Group> groups() { return kGroups; }

const char *none_glyph() { return kNoneGlyph; }

const char *glyph(std::string_view id) {
  for (const auto &group : kGroups) {
    for (const auto &icon : group.icons) {
      if (icon.id == id) {
        return icon.glyph;
      }
    }
  }
  return kNoneGlyph;
}

bool matches(std::string_view id, std::string_view filter) {
  if (filter.empty()) {
    return true;
  }
  return std::search(id.begin(), id.end(), filter.begin(), filter.end(),
                     [](char a, char b) { return lower(a) == lower(b); }) !=
         id.end();
}

} // namespace ui::instrument_icons
