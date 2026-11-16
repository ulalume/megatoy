#pragma once

#include <string>
#include <string_view>

namespace megatoy {
namespace system {

struct OpenBrowserResult {
  bool success = false;
  std::string error_message;
};

// Safe to use with untrusted URLs: validates the input, and uses
// platform-native APIs where possible.
OpenBrowserResult open_default_browser(std::string_view url);

} // namespace system
} // namespace megatoy
