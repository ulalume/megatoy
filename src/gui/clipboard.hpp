#pragma once

#include <string_view>

namespace platform::clipboard {

// Copy text to the clipboard. On Web, falls back to a manual overlay if denied.
void copy_text(std::string_view text);

} // namespace platform::clipboard
