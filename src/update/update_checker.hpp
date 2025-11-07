#pragma once

#include <string>
#include <string_view>

namespace update {

struct UpdateCheckResult {
  bool success = false;
  bool update_available = false;
  std::string latest_version;
  std::string release_url;
  std::string error_message;
};

UpdateCheckResult check_for_updates(std::string_view current_version_tag);

} // namespace update
