#include "core/random_utils.hpp"
#include <cassert>
#include <iostream>

void test_range_clamp() {
  core::Range r{0, 10};
  assert(r.clamp(-5) == 0);
  assert(r.clamp(5) == 5);
  assert(r.clamp(15) == 10);
  std::cout << "test_range_clamp passed" << std::endl;
}

void test_range_random() {
  core::Range r{5, 10};
  std::uint32_t seed = 12345;
  auto rng = core::make_rng(static_cast<int>(seed), seed);

  for (int i = 0; i < 100; ++i) {
    int val = r.random(rng);
    assert(val >= 5 && val <= 10);
  }
  std::cout << "test_range_random passed" << std::endl;
}

void test_lerp() {
  assert(core::lerp_value(0, 10, 0.0f) == 0);
  assert(core::lerp_value(0, 10, 1.0f) == 10);
  assert(core::lerp_value(0, 10, 0.5f) == 5);
  std::cout << "test_lerp passed" << std::endl;
}

int main() {
  test_range_clamp();
  test_range_random();
  test_lerp();
  return 0;
}
