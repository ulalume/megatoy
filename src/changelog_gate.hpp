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
 * Only an upgrade is announced. Any other difference -- a downgrade, or a
 * stored version too malformed to compare -- is recorded silently: the notice
 * claims the app was updated, and it should not claim that when it cannot
 * tell.
 *
 * No stored version means either a fresh install or a build from before this
 * existed. Both are announced; the two are indistinguishable from here, so
 * the caller words the notice for both (see changelog_notice_message).
 *
 * Whatever is decided, the caller must record the version before the notice
 * can be acted on, or a user who ignores it gets told again on every launch.
 */
ChangelogAction decide_changelog_action(std::string_view stored_version,
                                        std::string_view current_version);

} // namespace megatoy
