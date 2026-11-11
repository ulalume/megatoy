#pragma once

#include "patches/patch_lab_sources.hpp"
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
  std::function<void(const std::string &label, const std::string &merge_key)>
      begin_history;
  std::function<void()> commit_history;
  /// Load a file dropped from outside into one place, asking which
  /// instrument where it holds several.
  std::function<void(const std::filesystem::path &,
                     std::function<void(const ym2612::Patch &,
                                        const std::filesystem::path &)>)>
      load_dropped_file;
};

/// A patch picked for Mix or Morph: a browser entry, read when the operation
/// runs, or an open document's patch as it was when picked. The document may
/// be the one the result goes into, so it is not read again.
struct PatchLabSource {
  std::string entry_path;
  std::optional<ym2612::Patch> patch;
  std::string label;
  /// The document's instrument icon when it was picked.
  std::string icon;

  bool empty() const { return entry_path.empty() && !patch; }
};

struct PatchLabState {
  /// How Mix makes its patch: each parameter from one source, or in between.
  enum class MixMode { Pick, Blend };
  /// The pages, in the order the list down the left shows them.
  enum class Page { Random, Mutate, Mix };

  /// Mutate's settings.
  struct MutateSettings {
    int amount = 2;
    float probability = 0.35f;
    bool allow_algorithm_change = true;
  };

  /// Mix's settings.
  struct MixSettings {
    PatchLabSource source_a;
    PatchLabSource source_b;
    MixMode mode = MixMode::Pick;
    float blend = 0.5f;
    bool interpolate_algorithm = true;
  };

  struct SessionResult {
    std::uint64_t id = 0;
    Page page = Page::Random;
    /// What it was made with, shown above when it was made; empty when the
    /// page has nothing to set.
    std::string settings;
    std::string created_at;
    ym2612::Patch patch;
    // The pages' settings it was made with, its own page's put back when it
    // is chosen.
    MutateSettings mutate;
    MixSettings mix;
  };

  Page page = Page::Random;

  // Generated patches live only for this app session. New results are
  // inserted at the front so the most recent experiment is always visible.
  std::vector<SessionResult> results;
  std::uint64_t next_result_id = 1;
  /// The result last made or chosen, while its page is shown.
  std::optional<std::uint64_t> chosen_result;

  // Randomize
  int random_template_iterations = 4;

  // Mix
  MixSettings mix;
  /// The patches a source can be picked from, as of `entries_revision` of the
  /// repository. They point into its tree, which only changes with it.
  std::vector<EntryDisplay> entries;
  std::uint64_t entries_revision = 0;
  std::string mix_error;

  // Mutate
  MutateSettings mutate;

  // Search text of the open source list; one list is open at a time.
  std::string picker_filter;
};

void render_patch_lab(const char *title, PatchLabContext &context,
                      PatchLabState &state);

} // namespace ui
