#pragma once

// The patch browser's search and star predicates. Split out of
// patch_selector_shared.hpp so the tree flattening pass -- and its unit test
// -- can use them without pulling in ImGui.

#include "patches/patch_repository.hpp"

#include <string>

namespace ui::selector_detail {

std::string to_lower(const std::string &value);
bool contains_case_insensitive(const std::string &haystack,
                               const std::string &needle_lower);

bool entry_passes_star_filter(const patches::PatchEntry &entry,
                              int min_star_rating);
bool entry_matches_query(const patches::PatchEntry &entry,
                         const std::string &query_lower);

} // namespace ui::selector_detail
