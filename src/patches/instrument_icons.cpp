#include "instrument_icons.hpp"

#include <algorithm>
#include <cctype>

namespace patches::instrument_icons {

namespace {
#include "patches/instrument_icon_table.inc"

/// The group holding `id`, or none.
const Group *find_group(std::string_view id) {
  for (const auto &group : kGroups) {
    for (const auto &icon : group.icons) {
      if (icon.id == id) {
        return &group;
      }
    }
  }
  return nullptr;
}

/// Lower case, with a hyphen read as a space.
char folded(char c) {
  return c == '-'
             ? ' '
             : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool contains(std::string_view text, std::string_view filter) {
  return std::search(text.begin(), text.end(), filter.begin(), filter.end(),
                     [](char a, char b) { return folded(a) == folded(b); }) !=
         text.end();
}
} // namespace

std::span<const Group> groups() { return kGroups; }

const char *none_glyph() { return kNoneGlyph; }

const Icon *find(std::string_view id) {
  if (const Group *group = find_group(id)) {
    for (const auto &icon : group->icons) {
      if (icon.id == id) {
        return &icon;
      }
    }
  }
  return nullptr;
}

const char *glyph(std::string_view id) {
  const Icon *icon = find(id);
  return icon != nullptr ? icon->glyph : kNoneGlyph;
}

bool known(std::string_view id) { return find(id) != nullptr; }

std::string display_name(std::string_view id) {
  // Initialisms, which a capital at the start of each word does not spell.
  if (id == "fx" || id == "ufo") {
    std::string upper(id);
    std::transform(upper.begin(), upper.end(), upper.begin(), [](char c) {
      return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    });
    return upper;
  }
  std::string name(id);
  bool word_start = true;
  for (char &c : name) {
    if (c == '-') {
      c = ' ';
      word_start = true;
    } else if (word_start) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      word_start = false;
    }
  }
  return name;
}

std::string_view group_name(std::string_view id) {
  const Group *group = find_group(id);
  return group != nullptr ? group->name : std::string_view();
}

bool matches(std::string_view id, std::string_view filter) {
  if (filter.empty()) {
    return true;
  }
  // The name differs from the id only in case and spaces, which the
  // comparison ignores.
  return contains(id, filter) || contains(group_name(id), filter);
}

} // namespace patches::instrument_icons
