#include "patch_hash.hpp"

#include <nlohmann/json.hpp>
#include <sstream>

namespace patches {

std::string calculate_patch_hash(const ym2612::Patch &patch) {
  // Create a JSON representation of the patch without the name
  // This ensures consistent hash calculation regardless of patch name
  nlohmann::json patch_data;

  // Include only the musical content for hash calculation
  patch_data["global"] = patch.global;
  patch_data["channel"] = patch.channel;
  patch_data["instrument"] = patch.instrument;

  // Convert to compact string for hashing
  std::string patch_str = patch_data.dump();

  // Calculate hash using standard library
  std::hash<std::string> hasher;
  std::size_t hash_value = hasher(patch_str);

  // Convert to hexadecimal string
  std::ostringstream ss;
  ss << std::hex << hash_value;

  return ss.str();
}

} // namespace patches
