#pragma once

#include <string_view>

namespace megatoy {

enum class ChangelogAction {
  /// Say nothing and leave the stored version alone.
  Nothing,
  /// Remember this version without telling the user about it.
  RecordOnly,
  /// Announce it, and remember the version.
  Show,
};

/**
 * Decide what to do about the change log at startup.
 *
 * Only an upgrade is announced; a downgrade or an unreadable stored version
 * is recorded silently, since the notice claims the app was updated. No
 * stored version means a fresh install or a build that predates the change
 * log -- indistinguishable here, so both are announced and the caller words
 * it.
 *
 * The caller must record the version whatever it decides, or a user who
 * ignores the notice gets it again on every launch.
 */
ChangelogAction decide_changelog_action(std::string_view stored_version,
                                        std::string_view current_version);

} // namespace megatoy
