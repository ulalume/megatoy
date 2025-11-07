#pragma once

#include <string>
#include <string_view>

namespace megatoy {
namespace system {

// Result of attempting to open a URL in the default browser
struct OpenBrowserResult {
  bool success = false;
  std::string error_message;
};

// Opens the given URL in the system's default browser
// This function is safe to use with untrusted URLs as it properly escapes
// and validates input, and uses platform-native APIs where possible.
OpenBrowserResult open_default_browser(std::string_view url);

} // namespace system
} // namespace megatoy
