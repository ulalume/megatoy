#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ym2612 {
struct Patch;
}

namespace platform::web::patch_store {

struct SavedPatchInfo {
  std::string id;
  std::string name;
};

std::vector<SavedPatchInfo> list();
bool save(const ym2612::Patch &patch, const std::string &display_name);
bool load(const std::string &id, ym2612::Patch &out_patch);
bool remove(const std::string &id);
bool exists(const std::string &id);

} // namespace platform::web::patch_store
