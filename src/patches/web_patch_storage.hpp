#pragma once

#include "patch_storage.hpp"

#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_patch_store.hpp"

namespace patches {

class WebPatchStorage final : public PatchStorage {
public:
  void append_entries(std::vector<PatchEntry> &tree) const override;
  bool load_patch(const PatchEntry &entry,
                  ym2612::Patch &out_patch) const override;
};

} // namespace patches

#endif
