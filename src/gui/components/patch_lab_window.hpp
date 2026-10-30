#pragma once

#include "patches/patch_repository.hpp"
#include "patches/patch_session.hpp"
#include "preferences/preference_manager.hpp"
#include "ym2612/patch.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ui {

struct PatchLabContext {
  patches::PatchSession &session;
  PreferenceManager::UIPreferences &prefs;
  std::function<void(const std::string &label, const std::string &merge_key,
                     const ym2612::Patch &before)>
      begin_history;
  std::function<void()> commit_history;
};

/// A patch picked for Mix or Morph: a browser entry, read when the operation
/// runs, or an open document's patch as it was when picked. The document may
/// be the one the result goes into, so it is not read again.
struct PatchLabSource {
  std::string entry_path;
  std::optional<ym2612::Patch> patch;
  std::string label;

  bool empty() const { return entry_path.empty() && !patch; }
};

struct PatchLabState {
  struct SessionResult {
    std::uint64_t id = 0;
    std::string label;
    std::string created_at;
    ym2612::Patch patch;
  };

  // Generated patches live only for this app session. New results are
  // inserted at the front so the most recent experiment is always visible.
  std::vector<SessionResult> results;
  std::uint64_t next_result_id = 1;

  // Randomize
  int random_template_iterations = 4;

  // Shared sources for blend/morph
  PatchLabSource source_a;
  PatchLabSource source_b;
  std::string merge_error;

  float morph_mix = 0.5f;
  bool morph_interpolate_algorithm = true;
  std::string morph_error;

  // Mutate
  int mutate_amount = 2;
  float mutate_probability = 0.35f;
  bool mutate_allow_algorithm_change = true;

  // Search text of the open source list; one list is open at a time.
  std::string picker_filter;
};

void render_patch_lab(const char *title, PatchLabContext &context,
                      PatchLabState &state);

} // namespace ui
