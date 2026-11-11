#pragma once

#include <filesystem>
#include <optional>

namespace platform::trash {

/// Whether deleting moves files to the system's Trash, to be put back from
/// there.
bool available();

/// Moves `path`, a file or a folder, to the Trash and returns where the Trash
/// keeps it. Nothing when it cannot go there.
std::optional<std::filesystem::path>
move_to_trash(const std::filesystem::path &path);

} // namespace platform::trash
