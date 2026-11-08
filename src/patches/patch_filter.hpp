#pragma once

// The patch browser's search and star predicates.

#include "patches/patch_repository.hpp"

#include <string>

namespace patches {

std::string to_lower(const std::string &value);
/// What a search box's text is matched as: lower case, without the spaces
/// around it.
std::string search_key(const std::string &query);

/**
 * A search box's text as the browser reads it: a word "*1" to "*5" asks for
 * at least that many stars, and the rest is looked for in names.
 */
struct SearchQuery {
  /// The rest, as search_key gives it.
  std::string text_lower;
  /// The last rating word's, or 0 without one.
  int min_stars = 0;
};
SearchQuery parse_search(const std::string &query);
/// `query` asking for at least `stars`: its rating word changed, or one put
/// in front of it.
std::string with_star_word(const std::string &query, int stars);

bool contains_case_insensitive(const std::string &haystack,
                               const std::string &needle_lower);

bool entry_passes_star_filter(const PatchEntry &entry, int min_star_rating);
bool entry_matches_query(const PatchEntry &entry,
                         const std::string &query_lower);

} // namespace patches
