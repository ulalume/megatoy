#include "patch_lab_window.hpp"

#include "common.hpp"
#include "imgui_internal.h"
#include "patches/patch_lab.hpp"
#include <IconsFontAwesome7.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <imgui.h>
#include <vector>

namespace ui {
namespace {

using EntryPtr = const patches::PatchEntry *;

struct EntryDisplay {
  EntryPtr entry = nullptr;
  std::string label;
};

void collect_entries(const std::vector<patches::PatchEntry> &tree,
                     std::vector<EntryDisplay> &out) {
  for (const auto &entry : tree) {
    if (entry.is_directory) {
      collect_entries(entry.children, out);
    } else {
      EntryDisplay display;
      display.entry = &entry;
      if (!entry.relative_path.empty()) {
        display.label = display_preset_path(entry.relative_path);
      } else if (!entry.name.empty()) {
        display.label = entry.name;
      } else {
        display.label = "(unnamed patch)";
      }
      out.push_back(std::move(display));
    }
  }
}

std::vector<EntryDisplay>
build_entry_list(const patches::PatchRepository &repository) {
  std::vector<EntryDisplay> entries;
  collect_entries(repository.tree(), entries);
  std::sort(entries.begin(), entries.end(),
            [](const EntryDisplay &lhs, const EntryDisplay &rhs) {
              return lhs.label < rhs.label;
            });
  return entries;
}

EntryPtr sanitize_selection(EntryPtr selection,
                            const std::vector<EntryDisplay> &entries) {
  const bool contains = std::any_of(entries.begin(), entries.end(),
                                    [selection](const EntryDisplay &item) {
                                      return item.entry == selection;
                                    });
  if (contains || entries.empty()) {
    return contains ? selection : nullptr;
  }
  return entries.front().entry;
}

std::string combo_preview_label(EntryPtr selection,
                                const std::vector<EntryDisplay> &entries) {
  if (!selection) {
    return "<select>";
  }
  auto it = std::find_if(entries.begin(), entries.end(),
                         [selection](const EntryDisplay &item) {
                           return item.entry == selection;
                         });
  if (it != entries.end()) {
    return it->label;
  }
  return "<missing>";
}

bool render_patch_combo(const char *label,
                        const std::vector<EntryDisplay> &entries,
                        EntryPtr &selection, std::string &filter_text) {
  const std::string preview = combo_preview_label(selection, entries);
  bool changed = false;
  bool filter_active = !filter_text.empty();

  if (ImGui::BeginCombo(label, preview.c_str())) {
    // Add filter input at the top of the combo
    ImGui::SetNextItemWidth(-1.0f);

    // Use char buffer for ImGui compatibility
    static char filter_buffer[256];
    strncpy(filter_buffer, filter_text.c_str(), sizeof(filter_buffer) - 1);
    filter_buffer[sizeof(filter_buffer) - 1] = '\0';

    if (ImGui::InputTextWithHint("##filter",
                                 ICON_FA_MAGNIFYING_GLASS " Search...",
                                 filter_buffer, sizeof(filter_buffer))) {
      filter_text = std::string(filter_buffer);
      filter_active = !filter_text.empty();
    }

    // Auto-focus the filter input when combo opens
    if (ImGui::IsWindowAppearing()) {
      ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::Separator();

    // Filter and display entries
    for (const auto &item : entries) {
      bool should_show = true;

      if (filter_active) {
        // Case-insensitive search
        std::string item_lower = item.label;
        std::string filter_lower = filter_text;
        std::transform(item_lower.begin(), item_lower.end(), item_lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        std::transform(filter_lower.begin(), filter_lower.end(),
                       filter_lower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        should_show = item_lower.find(filter_lower) != std::string::npos;
      }

      if (should_show) {
        const bool is_selected = selection == item.entry;
        if (ImGui::Selectable(item.label.c_str(), is_selected)) {
          selection = item.entry;
          changed = true;
          filter_text.clear();
        }
        if (is_selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
    }

    ImGui::EndCombo();
  }
  return changed;
}

bool load_patch_from_entry(const patches::PatchRepository &repository,
                           EntryPtr entry, ym2612::Patch &out_patch) {
  if (!entry) {
    return false;
  }
  return repository.load_patch(*entry, out_patch);
}

void apply_patch_result(PatchLabContext &context,
                        const std::string &history_label,
                        const std::string &history_key,
                        const ym2612::Patch &new_patch) {
  auto &session = context.session;
  auto before = session.current_patch();
  if (context.begin_history) {
    context.begin_history(history_label, history_key, before);
  }

  session.current_patch() = new_patch;
  session.current_patch().name = before.name;
  session.apply_patch_to_audio();

  if (context.commit_history) {
    context.commit_history();
  }
}

void render_random_section(PatchLabContext &context, PatchLabState &state) {
  ImGui::TextWrapped("Generates a new patch each time.");
  ImGui::Spacing();

  ImGui::SetNextItemWidth(120.0f);
  ImGui::InputInt("Seed (auto = -1)", &state.random_seed);

  if (ImGui::Button("Apply to current patch")) {
    state.random_template_iterations =
        std::clamp(state.random_template_iterations, 0, 8);
    patch_lab::RandomOptions options;
    options.seed = state.random_seed;
    options.mode = patch_lab::RandomOptions::Mode::Wild;
    options.mutate_iterations = state.random_template_iterations;

    auto result = patch_lab::random_patch(options);
    const auto key = "patch_lab.random: " + std::to_string(result.seed);
    apply_patch_result(context, "Patch Lab Randomize", key, result.patch);
    state.random_last_seed = result.seed;
    state.random_has_result = true;
  }

  if (state.random_has_result) {
    ImGui::Text("Last seed: %u", state.random_last_seed);
  }
}

void render_merge_section(PatchLabContext &context, PatchLabState &state,
                          const std::vector<EntryDisplay> &entries) {
  ImGui::TextWrapped(
      "Mixes two patches by randomly choosing parameters from Patch A and "
      "Patch B.");
  ImGui::Spacing();

  auto sanitized_a = sanitize_selection(state.source_a, entries);
  auto sanitized_b = sanitize_selection(state.source_b, entries);
  state.source_a = sanitized_a;
  state.source_b = sanitized_b;

  if (entries.empty()) {
    ImGui::TextUnformatted("No patches available.");
    ImGui::BeginDisabled(true);
  }

  render_patch_combo("Patch A", entries, state.source_a,
                     state.combo_filter_patch_a);
  render_patch_combo("Patch B", entries, state.source_b,
                     state.combo_filter_patch_b);

  ImGui::SetNextItemWidth(120.0f);
  ImGui::InputInt("Seed (auto = -1)##merge", &state.merge_seed);

  if (!entries.empty()) {
    if (ImGui::Button("Merge into current patch")) {
      ym2612::Patch patch_a;
      ym2612::Patch patch_b;
      auto &repository = context.session.repository();
      bool loaded_a =
          load_patch_from_entry(repository, state.source_a, patch_a);
      bool loaded_b =
          load_patch_from_entry(repository, state.source_b, patch_b);
      if (!loaded_a || !loaded_b) {
        state.merge_error = "Failed to load one or both patches.";
      } else {
        patch_lab::MergeOptions options;
        options.seed = state.merge_seed;
        auto result = patch_lab::merge(patch_a, patch_b, options);
        apply_patch_result(context, "Patch Lab Merge", result.patch.hash(),
                           result.patch);
        state.merge_last_seed = result.seed;
        state.merge_has_result = true;
        state.merge_error.clear();
      }
    }
  }

  if (!entries.empty()) {
    if (state.merge_has_result) {
      ImGui::Text("Last seed: %u", state.merge_last_seed);
    }
    if (!state.merge_error.empty()) {
      ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%s",
                         state.merge_error.c_str());
    }
  } else {
    ImGui::EndDisabled();
  }
}

void render_morph_section(PatchLabContext &context, PatchLabState &state,
                          const std::vector<EntryDisplay> &entries) {
  ImGui::TextWrapped(
      "Interpolates between Patch A and Patch B with the blend slider for "
      "smooth transitions.");
  ImGui::Spacing();

  auto sanitized_a = sanitize_selection(state.source_a, entries);
  auto sanitized_b = sanitize_selection(state.source_b, entries);
  state.source_a = sanitized_a;
  state.source_b = sanitized_b;

  if (entries.empty()) {
    ImGui::TextUnformatted("No patches available.");
    ImGui::BeginDisabled(true);
  }

  render_patch_combo("Patch A##morph", entries, state.source_a,
                     state.combo_filter_patch_a);
  render_patch_combo("Patch B##morph", entries, state.source_b,
                     state.combo_filter_patch_b);

  ImGui::SliderFloat("Blend", &state.morph_mix, 0.0f, 1.0f);
  ImGui::Checkbox("Interpolate algorithm", &state.morph_interpolate_algorithm);

  if (!entries.empty()) {
    if (ImGui::Button("Morph into current patch")) {
      ym2612::Patch patch_a;
      ym2612::Patch patch_b;
      auto &repository = context.session.repository();
      bool loaded_a =
          load_patch_from_entry(repository, state.source_a, patch_a);
      bool loaded_b =
          load_patch_from_entry(repository, state.source_b, patch_b);
      if (!loaded_a || !loaded_b) {
        state.morph_error = "Failed to load one or both patches.";
      } else {
        patch_lab::MorphOptions options;
        options.mix = state.morph_mix;
        options.interpolate_algorithm = state.morph_interpolate_algorithm;

        auto result = patch_lab::morph(patch_a, patch_b, options);
        apply_patch_result(context, "Patch Lab Morph", result.patch.hash(),
                           result.patch);
        state.morph_error.clear();
      }
    }
  }

  if (!entries.empty()) {
    if (!state.morph_error.empty()) {
      ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%s",
                         state.morph_error.c_str());
    }
  } else {
    ImGui::EndDisabled();
  }
}

void render_mutate_section(PatchLabContext &context, PatchLabState &state) {
  ImGui::TextWrapped(
      "Perturbs the current patch; adjust variation depth and probability, "
      "and optionally lock the algorithm.");
  ImGui::Spacing();

  state.mutate_amount = std::clamp(state.mutate_amount, 0, 12);
  state.mutate_probability = std::clamp(state.mutate_probability, 0.0f, 1.0f);

  ImGui::SliderInt("Variation depth", &state.mutate_amount, 0, 12);
  ImGui::SliderFloat("Probability", &state.mutate_probability, 0.0f, 1.0f);
  ImGui::Checkbox("Allow algorithm change",
                  &state.mutate_allow_algorithm_change);

  ImGui::SetNextItemWidth(120.0f);
  ImGui::InputInt("Seed (auto = -1)##mutate", &state.mutate_seed);

  if (ImGui::Button("Mutate Current Patch")) {
    patch_lab::MutateOptions options;
    options.seed = state.mutate_seed;
    options.amount = state.mutate_amount;
    options.probability = state.mutate_probability;
    options.allow_algorithm_variation = state.mutate_allow_algorithm_change;

    auto before = context.session.current_patch();
    auto result = patch_lab::mutate_in_place(before, options);
    apply_patch_result(context, "Patch Lab Mutate", before.hash(), before);
    state.mutate_last_seed = result.seed;
    state.mutate_has_result = true;
  }

  if (state.mutate_has_result) {
    ImGui::Text("Last seed: %u", state.mutate_last_seed);
  }
}

} // namespace

void render_patch_lab(const char *title, PatchLabContext &context,
                      PatchLabState &state) {
  if (!context.prefs.show_patch_lab) {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(400, 250), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin(title, &context.prefs.show_patch_lab)) {
    ImGui::End();
    return;
  }

  if (ImGui::BeginTabBar("##Patch Lab Operators",
                         ImGuiTabBarFlags_SaveSettings)) {
    auto &repository = context.session.repository();
    auto entries = build_entry_list(repository);
    if (ImGui::BeginTabItem(ICON_FA_DICE " Random")) {
      render_random_section(context, state);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(ICON_FA_SHUFFLE " Mix")) {
      render_merge_section(context, state, entries);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(ICON_FA_FLASK_VIAL " Morph")) {
      render_morph_section(context, state, entries);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(ICON_FA_VIRUS " Mutate")) {
      render_mutate_section(context, state);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::End();
}

} // namespace ui
