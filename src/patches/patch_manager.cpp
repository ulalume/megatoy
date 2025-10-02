#include "patch_manager.hpp"

#include <iostream>
#include <utility>

namespace patches {

PatchManager::PatchManager(megatoy::system::DirectoryService &directories)
    : directories_(directories),
      repository_(directories_.paths().patches_root,
                  directories_.paths().builtin_presets_root) {}

ym2612::Patch &PatchManager::current_patch() { return current_patch_; }

const ym2612::Patch &PatchManager::current_patch() const {
  return current_patch_;
}

const std::string &PatchManager::current_patch_path() const {
  return current_patch_path_;
}

void PatchManager::set_current_patch_path(const std::filesystem::path &path) {
  if (path.empty()) {
    current_patch_path_.clear();
  } else {
    current_patch_path_ = path.generic_string();
  }
}

bool PatchManager::load_patch(const PatchEntry &entry) {
  ym2612::Patch loaded_patch;
  if (!repository_.load_patch(entry, loaded_patch)) {
    return false;
  }

  current_patch_ = loaded_patch;
  current_patch_path_ = entry.relative_path;
  return true;
}

void PatchManager::refresh_directories() {
  repository_ = PatchRepository(directories_.paths().patches_root,
                                directories_.paths().builtin_presets_root);
}

PatchRepository &PatchManager::repository() { return repository_; }

const PatchRepository &PatchManager::repository() const { return repository_; }

} // namespace patches
