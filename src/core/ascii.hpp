#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace megatoy::ascii {

/// `value` with A-Z lowered, as extensions and search keys are compared.
inline std::string lower(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

} // namespace megatoy::ascii
