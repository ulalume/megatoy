#include "common.hpp"

namespace ym2612::formats::conversion {

uint8_t detune_from_dmp_to_patch(int dt) {
  switch (dt) {
  case 0: // -3
    return 7;
  case 1: // -2
    return 6;
  case 2: // -1
    return 5;
  case 3:     // 0
    return 4; // rym2612 zero maps to two values; default to 4
  case 4:     // 1
    return 1;
  case 5: // 2
    return 2;
  case 6: // 3
    return 3;
  case 7: // 3
    return 3;
  default:
    return 4; // Default fallback
  }
}

uint8_t detune_from_patch_to_dmp(int dt) {
  switch (dt) {
  case 7: // -3
    return 0;
  case 6: // -2
    return 1;
  case 5: // -1
    return 2;
  case 4: // 0
    return 3;
  case 3: // 3
    return 7;
  case 2: // 2
    return 6;
  case 1: // 1
    return 5;
  case 0: // 0
    return 4;
  default:
    return 4; // Default fallback
  }
}

} // namespace ym2612::formats::conversion
