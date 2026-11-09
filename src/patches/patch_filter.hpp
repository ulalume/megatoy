#pragma once

// The patch browser's search and star predicates.

#include "patches/patch_repository.hpp"

#include <string>

namespace patches {

std::string to_lower(const std::string &value);
/// What a search box's text is matched as: lower case, without the spaces
/// around it.
std::string search_key(const std::string &query);

/// The ratings a search shows: bit n for patches rated n stars, none set
/// for every patch.
struct StarFilter {
  unsigned ratings = 0;
  bool active() const { return ratings != 0; }
  bool operator==(const StarFilter &) const = default;
};
/// Patches rated `stars` or more; every patch for 0.
StarFilter at_least(int stars);
/// The `stars` that at_least() gives `filter` for, or 0 when none does.
int least_stars(StarFilter filter);

/**
 * A search box's text as the browser reads it: a word "*1" to "*5" shows
 * the patches rated that many stars, "*1+" to "*5+" those rated that many or
 * more, and the rest is looked for in names.
 */
struct SearchQuery {
  /// The rest, as search_key gives it.
  std::string text_lower;
  /// What the rating words show together.
  StarFilter stars;
};
SearchQuery parse_search(const std::string &query);
/// `query` showing the patches rated `stars` or more: its rating words
/// replaced by one, put in front when it has none, or taken out for 0.
std::string with_least_stars(const std::string &query, int stars);

bool contains_case_insensitive(const std::string &haystack,
                               const std::string &needle_lower);

bool entry_passes_star_filter(const PatchEntry &entry, StarFilter filter);
bool entry_matches_query(const PatchEntry &entry,
                         const std::string &query_lower);

} // namespace patches
