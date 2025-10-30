#pragma once

#include "ym2612/patch.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace patch_lab {

struct CategoryChoice {
  std::string id;
  std::string label;
};

struct MergeOptions {
  int seed = -1;
};

struct RandomOptions {
  enum class Mode {
    Wild,
    Category,
  };

  Mode mode = Mode::Wild;
  std::string category;
  int mutate_iterations = 0;
  int seed = -1;
};

struct MorphOptions {
  float mix = 0.5f;
  bool interpolate_algorithm = false;
};

struct MutateOptions {
  int seed = -1;
  int amount = 2;
  float probability = 0.35f;
  bool allow_algorithm_variation = true;
};

struct OperationResult {
  ym2612::Patch patch;
  std::uint32_t seed = 0;
};

struct MutateResult {
  std::uint32_t seed = 0;
};

std::vector<CategoryChoice> available_categories();

OperationResult merge(const ym2612::Patch &a, const ym2612::Patch &b,
                      const MergeOptions &options = {});

OperationResult random_patch(const RandomOptions &options = {});

OperationResult morph(const ym2612::Patch &a, const ym2612::Patch &b,
                      const MorphOptions &options = {});

MutateResult mutate_in_place(ym2612::Patch &patch,
                             const MutateOptions &options = {});

} // namespace patch_lab
