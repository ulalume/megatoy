#include "document_disk_versions.hpp"

#include "formats/patch_loader.hpp"
#include "formats/ym2612_format_adapter.hpp"
#include "patches/filename_utils.hpp"
#include "patches/patch_write.hpp"
#include <algorithm>
#include <utility>

namespace patches {

void DocumentDiskVersions::remember(const PatchDocument &document) {
  if (document.path().empty()) {
    disk_times_.erase(document.id());
    return;
  }
  std::error_code error;
  const auto time = std::filesystem::last_write_time(
      repository_.to_absolute_path(document.path()), error);
  if (error) {
    disk_times_.erase(document.id());
    return;
  }
  disk_times_[document.id()] = time;
}

std::vector<DocumentDiskVersions::DiskVersion>
DocumentDiskVersions::take(const Documents &documents) {
  std::erase_if(disk_times_, [&documents](const auto &entry) {
    return std::none_of(documents.begin(), documents.end(),
                        [&entry](const auto &document) {
                          return document->id() == entry.first;
                        });
  });
  std::vector<DiskVersion> versions;
  for (const auto &document : documents) {
    const auto recorded = disk_times_.find(document->id());
    if (recorded == disk_times_.end()) {
      continue;
    }
    const auto absolute = repository_.to_absolute_path(document->path());
    std::error_code error;
    const auto time = std::filesystem::last_write_time(absolute, error);
    if (error || time == recorded->second) {
      continue;
    }
    recorded->second = time;

    const auto &selection = document->selection_path().empty()
                                ? document->path()
                                : document->selection_path();
    const auto *entry = repository_.find_entry(selection);
    const auto format = formats::adapter::format_for_extension(
        lowercase_extension(document->path()));
    const bool multi_patch =
        format && formats::adapter::is_multi_patch(*format);
    ym2612::Patch patch;
    if (entry != nullptr) {
      if (!repository_.load_patch(*entry, patch)) {
        continue;
      }
    } else {
      // A file outside the workspace is not in the tree. An instrument picked
      // from a bank out there cannot be found again by its path.
      if (multi_patch ||
          !std::filesystem::path(document->path()).is_absolute()) {
        continue;
      }
      auto loaded = formats::load_patch_from_file(absolute);
      if (loaded.status != formats::PatchLoadStatus::Success ||
          loaded.patches.empty()) {
        continue;
      }
      patch = std::move(loaded.patches[0]);
    }
    // Named as opening the file names it, so only a change in the file
    // itself tells it apart.
    if (!multi_patch) {
      patch.name = absolute.stem().string();
    }
    if (patch != document->original_patch()) {
      versions.push_back({document->id(), std::move(patch)});
    }
  }
  return versions;
}

bool DocumentDiskVersions::outside_file_changed(
    const Documents &documents) const {
  for (const auto &document : documents) {
    const auto recorded = disk_times_.find(document->id());
    if (recorded == disk_times_.end() ||
        !std::filesystem::path(document->path()).is_absolute()) {
      continue;
    }
    std::error_code error;
    const auto time = std::filesystem::last_write_time(document->path(), error);
    if (!error && time != recorded->second) {
      return true;
    }
  }
  return false;
}

} // namespace patches
