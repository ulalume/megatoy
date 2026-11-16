#include "core/file_replace.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace megatoy::files {

bool replace(const std::filesystem::path &source,
             const std::filesystem::path &destination, std::error_code &error) {
#if defined(_WIN32)
  if (::MoveFileExW(source.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    error.clear();
    return true;
  }
  error = std::error_code(static_cast<int>(::GetLastError()),
                          std::system_category());
  return false;
#else
  std::filesystem::rename(source, destination, error);
  return !error;
#endif
}

bool write_through_temporary(
    const std::filesystem::path &path,
    const std::function<bool(const std::filesystem::path &temporary)> &write) {
  const auto temporary =
      path.parent_path() / ("." + path.filename().string() + ".tmp");
  std::error_code error;
  if (write(temporary) && replace(temporary, path, error)) {
    return true;
  }
  std::filesystem::remove(temporary, error);
  return false;
}

} // namespace megatoy::files
