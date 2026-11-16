#pragma once

#include <filesystem>
#include <functional>
#include <system_error>

namespace megatoy::files {

/// Moves `source` over `destination`, taking the place of a file there.
bool replace(const std::filesystem::path &source,
             const std::filesystem::path &destination, std::error_code &error);

/**
 * Has `write` write the file at a temporary path beside `path`, then moves
 * that over `path`: a write that fails or is interrupted leaves what was at
 * `path` as it was. The temporary is hidden, so a scan of the folder passes
 * it by. True once `path` holds what was written.
 */
bool write_through_temporary(
    const std::filesystem::path &path,
    const std::function<bool(const std::filesystem::path &temporary)> &write);

} // namespace megatoy::files
