#include "workspace/path_policy.hpp"

#include <system_error>

namespace megatoy::workspace {

std::filesystem::path normalize_path(const std::filesystem::path &path) {
  std::error_code ec;
  auto resolved = std::filesystem::weakly_canonical(path, ec);
  if (ec) {
    return path.lexically_normal();
  }
  return resolved;
}

bool paths_equal(const std::filesystem::path &lhs,
                 const std::filesystem::path &rhs) {
  return normalize_path(lhs) == normalize_path(rhs);
}

} // namespace megatoy::workspace
