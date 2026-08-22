#pragma once

#include <span>
#include <string_view>

/**
 * What each release changed, in the user's terms. Hand-written, newest first,
 * and not generated from the GitHub release notes: those list merged pull
 * requests, which is the wrong unit and the wrong language.
 *
 * `version` must match MEGATOY_VERSION_TAG for the release it describes, or
 * the update notice points at a heading that is not there.
 */
namespace megatoy {

struct ChangelogItem {
  std::string_view text;
  /// Sub-points, for a change too broad for one line.
  std::span<const std::string_view> details;
};

struct ChangelogEntry {
  std::string_view version;
  std::span<const ChangelogItem> items;
};

std::span<const ChangelogEntry> changelog();

} // namespace megatoy
