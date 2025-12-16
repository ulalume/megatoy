#pragma once

#include "patch_repository.hpp"
#include "patch_storage.hpp"
#include "platform/platform_config.hpp"
#include <string_view>

#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_patch_store.hpp"

namespace patches {

class WebPatchStorage final : public PatchStorage {
public:
  void append_entries(std::vector<PatchEntry> &tree) const override;
  bool load_patch(const PatchEntry &entry,
                  ym2612::Patch &out_patch) const override;
  bool remove_patch(const PatchEntry &entry) override;
  SavePatchResult save_patch(const ym2612::Patch &patch,
                             const std::string &name,
                             bool overwrite,
                             std::string_view preferred_extension) override;
  bool update_patch_metadata(const std::string &relative_path,
                             const PatchMetadata &metadata) override;
  std::optional<PatchMetadata>
  get_patch_metadata(const std::string &relative_path) const override;
  void cleanup_metadata(const std::vector<std::string> &paths) const override;
  bool is_writable() const override { return true; }
  std::string_view label() const override { return "localStorage"; }
  std::optional<bool>
  has_patch_named(const std::string &name) const override;
  bool download_patch(const ym2612::Patch &patch, const std::string &name,
                      const std::string &extension_hint) override;
  std::optional<std::filesystem::path>
  to_relative_path(const std::filesystem::path &path) const override;
  std::optional<std::filesystem::path>
  to_absolute_path(const std::filesystem::path &path) const override;
};

} // namespace patches

#endif
