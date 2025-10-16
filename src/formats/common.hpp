#pragma once

#include <cstdint>

namespace formats {

uint8_t detune_from_dmp_to_patch(int dt);
uint8_t detune_from_patch_to_dmp(int dt);

} // namespace formats
