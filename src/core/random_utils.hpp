#pragma once

#include <cmath>
#include <random>

namespace core {

struct Range {
  int min;
  int max;

  int clamp(int value) const;
  int random(std::mt19937 &rng) const;
  int random_biased(std::mt19937 &rng, float exponent,
                    bool bias_to_min = true) const;
};

std::mt19937 make_rng(int seed, std::uint32_t &resolved_seed);

bool random_bool(std::mt19937 &rng, double probability = 0.5);

template <typename T> T lerp_value(T a, T b, float t) {
  return static_cast<T>(
      std::round(static_cast<float>(a) +
                 (static_cast<float>(b) - static_cast<float>(a)) * t));
}

} // namespace core
