#include "platform/trash.hpp"

namespace platform::trash {

// Deleting is for good here; what is deleted is kept in memory for a while to
// be undone.

bool available() { return false; }

std::optional<std::filesystem::path>
move_to_trash(const std::filesystem::path &) {
  return std::nullopt;
}

} // namespace platform::trash
