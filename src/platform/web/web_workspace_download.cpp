#include "platform/web/web_workspace_download.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include "patches/filename_utils.hpp"
#include "platform/virtual_file_system.hpp"
#include "platform/web/web_download.hpp"
#include <algorithm>
#include <iterator>
#include <miniz.h>
#include <string>
#include <vector>

namespace platform::web {
namespace {

std::vector<uint8_t> read_bytes(const VirtualFileSystem &vfs,
                                const std::filesystem::path &path) {
  auto stream = vfs.open_read(path);
  if (!stream) {
    return {};
  }
  return {std::istreambuf_iterator<char>(*stream),
          std::istreambuf_iterator<char>()};
}

bool add_directory(mz_zip_archive &archive, const VirtualFileSystem &vfs,
                   const std::filesystem::path &directory,
                   const std::string &archive_prefix) {
  const std::string directory_entry = archive_prefix + "/";
  if (!mz_zip_writer_add_mem(&archive, directory_entry.c_str(), nullptr, 0,
                             MZ_NO_COMPRESSION)) {
    return false;
  }
  auto entries = vfs.read_directory(directory);
  std::sort(entries.begin(), entries.end(), [](const auto &lhs, const auto &rhs) {
    return lhs.path.filename().generic_string() <
           rhs.path.filename().generic_string();
  });

  for (const auto &entry : entries) {
    const std::string name = archive_prefix + "/" +
                             entry.path.filename().generic_string();
    if (entry.is_directory) {
      if (!add_directory(archive, vfs, entry.path, name)) {
        return false;
      }
      continue;
    }
    if (!entry.is_regular_file) {
      continue;
    }
    auto bytes = read_bytes(vfs, entry.path);
    if (bytes.empty() && vfs.exists(entry.path)) {
      auto stream = vfs.open_read(entry.path);
      if (!stream) {
        return false;
      }
    }
    if (!mz_zip_writer_add_mem(&archive, name.c_str(), bytes.data(),
                               bytes.size(), MZ_BEST_SPEED)) {
      return false;
    }
  }
  return true;
}

} // namespace

bool download_workspace_path(const VirtualFileSystem &vfs,
                             const std::filesystem::path &path) {
  if (!vfs.exists(path)) {
    return false;
  }

  if (!vfs.is_directory(path)) {
    auto bytes = read_bytes(vfs, path);
    if (bytes.empty()) {
      auto stream = vfs.open_read(path);
      if (!stream) {
        return false;
      }
    }
    download_binary(path.filename().generic_string(), bytes,
                    "application/octet-stream");
    return true;
  }

  mz_zip_archive archive{};
  if (!mz_zip_writer_init_heap(&archive, 0, 0)) {
    return false;
  }

  std::string root_name =
      patches::sanitize_filename(path.filename().generic_string());
  if (root_name.empty()) {
    root_name = "patches";
  }
  const bool added = add_directory(archive, vfs, path, root_name);
  void *data = nullptr;
  size_t size = 0;
  const bool finalized =
      added && mz_zip_writer_finalize_heap_archive(&archive, &data, &size);
  mz_zip_writer_end(&archive);
  if (!finalized || data == nullptr) {
    if (data != nullptr) {
      mz_free(data);
    }
    return false;
  }

  const auto *first = static_cast<const uint8_t *>(data);
  std::vector<uint8_t> bytes(first, first + size);
  mz_free(data);
  download_binary(root_name + ".zip", bytes, "application/zip");
  return true;
}

} // namespace platform::web

#endif
