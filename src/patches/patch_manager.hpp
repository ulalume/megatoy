#pragma once

#include "../system/directory_service.hpp"
#include "../ym2612/patch.hpp"
#include "patch_repository.hpp"

#include <filesystem>
#include <string>

namespace patches {

class PatchManager {
public:
  explicit PatchManager(megatoy::system::DirectoryService &directories);

  ym2612::Patch &current_patch();
  const ym2612::Patch &current_patch() const;

  const std::string &current_patch_path() const;
  void set_current_patch_path(const std::filesystem::path &path);

  bool load_patch(const PatchEntry &entry);

  void refresh_directories();

  PatchRepository &repository();
  const PatchRepository &repository() const;

private:
  megatoy::system::DirectoryService &directories_;
  PatchRepository repository_;
  ym2612::Patch current_patch_;
  std::string current_patch_path_;
};

} // namespace patches
