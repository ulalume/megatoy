#include "changelog_gate.hpp"
#include "update/version.hpp"

namespace megatoy {

ChangelogAction decide_changelog_action(std::string_view stored_version,
                                        std::string_view current_version) {
  if (current_version.empty()) {
    // Nothing worth recording, and nothing to compare against.
    return ChangelogAction::Nothing;
  }
  if (stored_version.empty()) {
    return ChangelogAction::Show;
  }
  if (stored_version == current_version) {
    return ChangelogAction::Nothing;
  }
  const auto is_newer =
      update::is_version_newer(current_version, stored_version);
  return is_newer.value_or(false) ? ChangelogAction::Show
                                  : ChangelogAction::RecordOnly;
}

} // namespace megatoy
