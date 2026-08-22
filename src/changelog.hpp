#pragma once

#include <span>
#include <string_view>

/**
 * What each release changed, in the user's terms.
 *
 * Hand-written, newest first, and deliberately not generated from the GitHub
 * release notes: those list merged pull requests, which is the wrong unit and
 * the wrong language for someone who just wants to know whether anything they
 * care about moved. Entries here are picked from those notes and reworded;
 * work with no visible effect is left out entirely.
 *
 * `version` must match MEGATOY_VERSION_TAG for the release it describes, or
 * the update notice will point at a heading that is not there.
 */
namespace megatoy {

struct ChangelogItem {
  std::string_view text;
  /// Sub-points, for a change too broad to land in one line.
  std::span<const std::string_view> details;
};

struct ChangelogEntry {
  std::string_view version;
  std::span<const ChangelogItem> items;
};

std::span<const ChangelogEntry> changelog();

} // namespace megatoy
