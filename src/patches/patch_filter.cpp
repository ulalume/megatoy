#include "patch_filter.hpp"

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
  return entry.metadata &&
         contains_case_insensitive(entry.metadata->category, query_lower);
}

} // namespace patches
