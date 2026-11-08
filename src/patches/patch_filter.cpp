#include "patch_filter.hpp"

#include "patches/instrument_icons.hpp"

#include <cctype>

namespace patches {

std::string to_lower(const std::string &value) {
  std::string lowered;
  lowered.reserve(value.size());
  for (unsigned char ch : value) {
    lowered.push_back(static_cast<char>(std::tolower(ch)));
  }
  return lowered;
}

std::string search_key(const std::string &query) {
  const auto first = query.find_first_not_of(" \t");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = query.find_last_not_of(" \t");
  return to_lower(query.substr(first, last - first + 1));
}

namespace {

bool is_space(char ch) { return ch == ' ' || ch == '\t'; }

/// Calls `found(at, stars)` for each "*1".."*5" word of `query`, `at` being
/// where it starts.
template <typename Found>
void for_each_star_word(const std::string &query, Found found) {
  std::size_t i = 0;
  while (i < query.size()) {
    while (i < query.size() && is_space(query[i])) {
      ++i;
    }
    const std::size_t start = i;
    while (i < query.size() && !is_space(query[i])) {
      ++i;
    }
    if (i - start == 2 && query[start] == '*' && query[start + 1] >= '1' &&
        query[start + 1] <= '5') {
      found(start, query[start + 1] - '0');
    }
  }
}

} // namespace

SearchQuery parse_search(const std::string &query) {
  SearchQuery out;
  // The rest, each rating word left out with the spaces after it.
  std::string rest;
  std::size_t from = 0;
  for_each_star_word(query, [&](std::size_t at, int stars) {
    rest.append(query, from, at - from);
    from = at + 2;
    while (from < query.size() && is_space(query[from])) {
      ++from;
    }
    out.min_stars = stars;
  });
  rest.append(query, from);
  out.text_lower = search_key(rest);
  return out;
}

std::string with_star_word(const std::string &query, int stars) {
  std::string out = query;
  bool changed = false;
  for_each_star_word(query, [&](std::size_t at, int) {
    out[at + 1] = static_cast<char>('0' + stars);
    changed = true;
  });
  if (changed) {
    return out;
  }
  return "*" + std::to_string(stars) + " " + query;
}

bool contains_case_insensitive(const std::string &haystack,
                               const std::string &needle_lower) {
  if (needle_lower.empty()) {
    return true;
  }
  return to_lower(haystack).find(needle_lower) != std::string::npos;
}

bool entry_passes_star_filter(const PatchEntry &entry, int min_star_rating) {
  if (min_star_rating <= 0) {
    return true;
  }
  const int rating = entry.metadata ? entry.metadata->star_rating : 0;
  return rating >= min_star_rating;
}

bool entry_matches_query(const PatchEntry &entry,
                         const std::string &query_lower) {
  if (query_lower.empty()) {
    return true;
  }
  if (contains_case_insensitive(entry.name, query_lower)) {
    return true;
  }
  // By the icon's name or its group's, such as "drums" for a kick.
  return entry.metadata && !entry.metadata->icon.empty() &&
         instrument_icons::matches(entry.metadata->icon, query_lower);
}

} // namespace patches
