#include "platform/trash.hpp"

namespace platform::trash {

// Deleting is for good here; what is deleted is kept in memory for a while to
// be undone.

bool available() { return false; }

const char *name() { return "Trash"; }

std::optional<std::filesystem::path>
move_to_trash(const std::filesystem::path &) {
  return std::nullopt;
}

bool put_back(const std::filesystem::path &, const std::filesystem::path &) {
  return false;
}

} // namespace platform::trash
