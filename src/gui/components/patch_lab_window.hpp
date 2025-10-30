#pragma once

#include "patches/patch_repository.hpp"
#include "patches/patch_session.hpp"
#include "preferences/preference_manager.hpp"
#include "ym2612/patch.hpp"
#include <cstdint>
#include <functional>
#include <string>

namespace ui {

struct PatchLabContext {
  patches::PatchSession &session;
  PreferenceManager::UIPreferences &prefs;
  std::function<void(const std::string &label, const std::string &merge_key,
                     const ym2612::Patch &before)>
      begin_history;
  std::function<void()> commit_history;
};

struct PatchLabState {
  enum class Mode { Randomize = 0, Merge, Morph, Mutate };

  int mode = static_cast<int>(Mode::Randomize);

  // Randomize
  int random_seed = -1;
  int random_template_iterations = 4;
  std::uint32_t random_last_seed = 0;
  bool random_has_result = false;

  // Shared sources for blend/morph
  const patches::PatchEntry *source_a = nullptr;
  const patches::PatchEntry *source_b = nullptr;
  int merge_seed = -1;
  std::uint32_t merge_last_seed = 0;
  bool merge_has_result = false;
  std::string merge_error;

  float morph_mix = 0.5f;
  bool morph_interpolate_algorithm = true;
  std::string morph_error;

  // Mutate
  int mutate_seed = -1;
  std::uint32_t mutate_last_seed = 0;
  bool mutate_has_result = false;
  int mutate_amount = 2;
  float mutate_probability = 0.35f;
  bool mutate_allow_algorithm_change = true;
};

void render_patch_lab(const char *title, PatchLabContext &context,
                      PatchLabState &state);

} // namespace ui
