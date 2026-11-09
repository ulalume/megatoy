#include "patch_filter.hpp"

#include "patches/instrument_icons.hpp"

#include <cctype>
#include <cstddef>
#include <utility>

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

/// Calls `found(at, length, shown)` for each rating word of `query`, `at`
/// being where it starts.
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
    const std::size_t length = i - start;
    if ((length == 2 || (length == 3 && query[start + 2] == '+')) &&
        query[start] == '*' && query[start + 1] >= '1' &&
        query[start + 1] <= '5') {
      const int stars = query[start + 1] - '0';
      found(start, length,
            length == 3 ? at_least(stars) : StarFilter{1u << stars});
    }
  }
}

/// `query` without its rating words, each taken with the spaces after it,
/// and where the first stood.
std::pair<std::string, std::size_t>
without_star_words(const std::string &query) {
  std::string rest;
  std::size_t from = 0;
  std::size_t first = std::string::npos;
  for_each_star_word(query,
                     [&](std::size_t at, std::size_t length, StarFilter) {
                       if (first == std::string::npos) {
                         first = rest.size() + (at - from);
                       }
                       rest.append(query, from, at - from);
                       from = at + length;
                       while (from < query.size() && is_space(query[from])) {
                         ++from;
                       }
                     });
  rest.append(query, from);
  return {rest, first};
}

} // namespace

StarFilter at_least(int stars) {
  if (stars <= 0) {
    return {};
  }
  return StarFilter{(0x3Fu >> stars) << stars};
}

int least_stars(StarFilter filter) {
  for (int stars = 1; stars <= 5; ++stars) {
    if (at_least(stars) == filter) {
      return stars;
    }
  }
  return 0;
}

SearchQuery parse_search(const std::string &query) {
  SearchQuery out;
  for_each_star_word(query, [&](std::size_t, std::size_t, StarFilter shown) {
    out.stars.ratings |= shown.ratings;
  });
  out.text_lower = search_key(without_star_words(query).first);
  return out;
}

std::string with_least_stars(const std::string &query, int stars) {
  auto [rest, first] = without_star_words(query);
  if (stars <= 0) {
    return rest;
  }
  const std::string word = "*" + std::to_string(stars) + "+ ";
  return rest.insert(first == std::string::npos ? 0 : first, word);
}

bool contains_case_insensitive(const std::string &haystack,
                               const std::string &needle_lower) {
  if (needle_lower.empty()) {
    return true;
  }
  return to_lower(haystack).find(needle_lower) != std::string::npos;
}

bool entry_passes_star_filter(const PatchEntry &entry, StarFilter filter) {
  if (!filter.active()) {
    return true;
  }
  const int rating = entry.metadata ? entry.metadata->star_rating : 0;
  return rating >= 0 && rating <= 5 && ((filter.ratings >> rating) & 1u) != 0;
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
