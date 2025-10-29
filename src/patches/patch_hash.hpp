#pragma once

#include "ym2612/patch.hpp"
#include <string>

namespace patches {

/**
 * Calculate a hash value for a patch.
 * This hash is based on the patch's musical content (global, channel,
 * instrument settings) but excludes the name to ensure patches with identical
 * settings have the same hash.
 *
 * @param patch The patch to calculate hash for
 * @return SHA256 hash as hexadecimal string
 */
std::string calculate_patch_hash(const ym2612::Patch &patch);

} // namespace patches
