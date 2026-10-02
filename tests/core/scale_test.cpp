#include "../test_check.hpp"
#include "core/types.hpp"
#include <iostream>
#include <vector>

void test_major_blues() {
  const std::vector<Key> in_c{Key::C, Key::D, Key::D_SHARP,
                              Key::E, Key::G, Key::A};
  CHECK(keys_from_scale_and_key(Scale::MAJAR_BLUES, Key::C) == in_c);
  const std::vector<Key> in_a{Key::A,       Key::B, Key::C,
                              Key::C_SHARP, Key::E, Key::F_SHARP};
  CHECK(keys_from_scale_and_key(Scale::MAJAR_BLUES, Key::A) == in_a);
  std::cout << "test_major_blues passed" << std::endl;
}

void test_minor_blues() {
  const std::vector<Key> in_c{Key::C,       Key::D_SHARP, Key::F,
                              Key::F_SHARP, Key::G,       Key::A_SHARP};
  CHECK(keys_from_scale_and_key(Scale::MINOR_BLUES, Key::C) == in_c);
  std::cout << "test_minor_blues passed" << std::endl;
}

int main() {
  test_major_blues();
  test_minor_blues();
  return 0;
}
