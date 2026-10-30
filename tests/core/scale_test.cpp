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

void test_every_scale_and_key() {
  const std::vector<std::vector<uint8_t>> expected = {
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
      {0, 2, 4, 5, 7, 9, 11},
      {0, 2, 3, 5, 7, 8, 10},
      {0, 2, 3, 5, 7, 8, 11},
      {0, 2, 3, 5, 7, 9, 10},
      {0, 1, 3, 5, 7, 8, 10},
      {0, 2, 4, 6, 7, 9, 11},
      {0, 2, 4, 5, 7, 9, 10},
      {0, 1, 3, 5, 6, 8, 10},
      {0, 2, 3, 4, 7, 9},
      {0, 3, 5, 6, 7, 10},
      {0, 2, 4, 7, 9},
      {0, 3, 5, 7, 10},
      {0, 2, 4, 6, 8, 10},
      {0, 4, 5, 7, 11},
  };
  CHECK(expected.size() == static_cast<std::size_t>(Scale::RYUKYU) + 1);
  for (std::size_t scale = 0; scale < expected.size(); ++scale) {
    for (const auto key : all_keys) {
      const auto actual =
          keys_from_scale_and_key(static_cast<Scale>(scale), key);
      CHECK(actual.size() == expected[scale].size());
      for (std::size_t i = 0; i < actual.size(); ++i) {
        // Chromatic ignores the selected key.
        const auto shift = scale == 0 ? uint8_t{0} : static_cast<uint8_t>(key);
        CHECK(actual[i] ==
              shift_key(static_cast<Key>(expected[scale][i]), shift));
      }
    }
  }
  for (const auto key : all_keys) {
    CHECK(keys_from_scale_and_key(static_cast<Scale>(15), key) ==
          keys_from_scale_and_key(Scale::CHROMATIC, Key::C));
  }
}

int main() {
  test_every_scale_and_key();
  test_major_blues();
  test_minor_blues();
  return 0;
}
