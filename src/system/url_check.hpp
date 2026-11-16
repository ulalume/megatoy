#pragma once

#include "core/ascii.hpp"

#include <string>
#include <string_view>

namespace megatoy::system {

/// Why `url` is not handed to the system to open, or empty when it is. Only
/// http and https are: file:// would give access to local files. Characters
/// a shell acts on are turned away too.
inline std::string url_problem(std::string_view url) {
  const std::string lowered = ascii::lower(std::string(url));
  if (!lowered.starts_with("http://") && !lowered.starts_with("https://")) {
    return "Invalid or unsupported URL scheme. Only http:// and "
           "https:// are allowed.";
  }
  if (url.find_first_of("\"';&|") != std::string_view::npos) {
    return "URL contains potentially dangerous characters";
  }
  return {};
}

} // namespace megatoy::system
