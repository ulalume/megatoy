#pragma once

#include <cstdint>

namespace parsers {
uint8_t convert_detune_from_dmp_to_patch(int dt);

uint8_t convert_detune_from_patch_to_dmp_fui(int dt);

} // namespace parsers
