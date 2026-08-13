#pragma once

#include <optional>
#include <string_view>

namespace update {

/**
 * Returns whether `candidate` is newer than `current`.
 *
 * Tags may have a leading `v` and may use Semantic Versioning prerelease and
 * build suffixes. A malformed tag returns std::nullopt instead of guessing.
 */
std::optional<bool> is_version_newer(std::string_view candidate,
                                     std::string_view current);

} // namespace update
