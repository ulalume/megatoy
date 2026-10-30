#pragma once

// The patch browser's search and star predicates.

#include "patches/patch_repository.hpp"

#include <string>

namespace patches {

std::string to_lower(const std::string &value);
/// What a search box's text is matched as: lower case, without the spaces
/// around it.
std::string search_key(const std::string &query);
bool contains_case_insensitive(const std::string &haystack,
                               const std::string &needle_lower);

bool entry_passes_star_filter(const PatchEntry &entry, int min_star_rating);
bool entry_matches_query(const PatchEntry &entry,
                         const std::string &query_lower);

} // namespace patches
