#pragma once

#include <filesystem>
#include <optional>

namespace platform::trash {

/// Whether deleting moves files to the system's Trash, to be put back from
/// there.
bool available();

/// What the system calls its Trash: "Recycle Bin" on Windows.
const char *name();

/// Moves `path`, a file or a folder, to the Trash and returns where the Trash
/// keeps it, or an empty path when that is not known. Nothing when it cannot
/// go there, and then nothing is deleted.
std::optional<std::filesystem::path>
move_to_trash(const std::filesystem::path &path);

/// Moves `in_trash`, as move_to_trash() returned it, back to `path`, and
/// drops what the Trash noted about it.
bool put_back(const std::filesystem::path &in_trash,
              const std::filesystem::path &path);

} // namespace platform::trash
