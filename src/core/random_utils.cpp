#include "core/random_utils.hpp"
#include <algorithm>

namespace core {

int Range::clamp(int value) const {
  if (value < min) {
    return min;
  }
  if (value > max) {
    return max;
  }
  return value;
}

int Range::random(std::mt19937 &rng) const {
  std::uniform_int_distribution<int> dist(min, max);
  return dist(rng);
}

int Range::random_biased(std::mt19937 &rng, float exponent,
                         bool bias_to_min) const {
  if (max <= min) {
    return min;
  }

  std::uniform_real_distribution<double> dist(0.0, 1.0);
  double u = dist(rng);
  double e = std::max(0.001, static_cast<double>(exponent));

  double sample = bias_to_min ? std::pow(u, e) : 1.0 - std::pow(1.0 - u, e);

  int value = static_cast<int>(std::llround(
      static_cast<double>(min) + sample * static_cast<double>(max - min)));
  return clamp(value);
}

std::mt19937 make_rng(int seed, std::uint32_t &resolved_seed) {
  if (seed < 0) {
    std::random_device rd;
    resolved_seed = rd();
  } else {
    resolved_seed = static_cast<std::uint32_t>(seed);
  }
  return std::mt19937{resolved_seed};
}

bool random_bool(std::mt19937 &rng, double probability) {
  std::bernoulli_distribution dist(probability);
  return dist(rng);
}

} // namespace core
