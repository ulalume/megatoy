#include "util.hpp"
// #include <iostream>

namespace parsers {
uint8_t convert_detune(int dt) {
  // std::cout << "Detune value: " << dt << std::endl;
  switch (dt) {
  case 0:
    return 7;
  case 1:
    return 6;
  case 2:
    return 5;
  case 3:
    return 4; // rym2612 zero maps to two values; default to 4
  case 4:
    return 1;
  case 5:
    return 2;
  case 6:
    return 3;
  case 7:
    return 3;
  default:
    return 4; // Default fallback
  }
}
} // namespace parsers
