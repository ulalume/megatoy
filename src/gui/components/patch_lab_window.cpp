#include "patch_lab_window.hpp"

#include "common.hpp"
#include "gui/areas.hpp"
#include "gui/ui_scale.hpp"
#include "gui/window_title.hpp"
#include "imgui_internal.h"
#include "patch_selector.hpp"
#include "patches/patch_lab.hpp"
#include "patches/patch_lab_sources.hpp"
#include <IconsFontAwesome7.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <imgui.h>
#include <optional>
#include <utility>
#include <vector>

namespace ui {
namespace {

const std::vector<EntryDisplay> &
current_entries(PatchLabState &state,
                const patches::PatchRepository &repository) {
  if (state.entries_revision != repository.revision()) {
    state.entries = build_entry_list(repository);
    state.entries_revision = repository.revision();
  }
  return state.entries;
}

/// What a source is called and what it holds; false when its entry has gone.
bool resolve_source(const patches::PatchSession &session,
                    const PatchLabSource &source, std::string *label,
                    ym2612::Patch *patch) {
  if (source.patch) {
    if (label != nullptr) {
      *label = source.label;
    }
    if (patch != nullptr) {
      *patch = *source.patch;
    }
    return true;
  }
  const auto *entry = session.find_entry(source.entry_path);
  if (entry == nullptr) {
    return false;
  }
  if (label != nullptr) {
    *label = entry->name.empty()
                 ? display_preset_path(session.repository().display_path(
                       entry->relative_path))
                 : entry->name;
  }
  return patch == nullptr || session.repository().load_patch(*entry, *patch);
}

/// The list a source slot opens: the open documents first, then every patch
/// in the browser.
bool render_source_list(const patches::PatchSession &session,
                        const std::vector<EntryDisplay> &entries,
                        PatchLabSource &source, std::string &filter_text) {
  // -FLT_MIN, not -1.0f: a negative width is relative to what is available,
  // so -1.0f makes this the widest content by one pixel less each frame.
  ImGui::SetNextItemWidth(-FLT_MIN);
  static char filter_buffer[256];
  std::snprintf(filter_buffer, sizeof(filter_buffer), "%s",
                filter_text.c_str());
  if (ImGui::InputTextWithHint("##filter",
                               ICON_FA_MAGNIFYING_GLASS " Search...",
                               filter_buffer, sizeof(filter_buffer))) {
    filter_text = filter_buffer;
  }
  if (ImGui::IsWindowAppearing()) {
    ImGui::SetKeyboardFocusHere(-1);
  }

  bool changed = false;
  const float max_height = ImGui::GetTextLineHeightWithSpacing() * 16.0f;
  ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f),
                                      ImVec2(FLT_MAX, max_height));
  if (ImGui::BeginChild("##sources", ImVec2(0.0f, 0.0f),
                        ImGuiChildFlags_AutoResizeY)) {
    const auto sources = patch_lab_sources(session, entries, filter_text);
    for (const auto &item : sources) {
      if (item.kind == SourceRowKind::Heading) {
        ImGui::SeparatorText(item.text().c_str());
        continue;
      }
      if (item.kind == SourceRowKind::Document) {
        ImGui::PushID(static_cast<int>(item.document->id()));
        if (ImGui::Selectable(item.label.c_str())) {
          source = {.patch = item.document->patch(), .label = item.label};
          changed = true;
        }
        ImGui::PopID();
        continue;
      }
      const auto &entry = *item.entry->entry;
      const bool selected = source.entry_path == entry.relative_path;
      if (ImGui::Selectable(item.text().c_str(), selected)) {
        source = {.entry_path = entry.relative_path};
        changed = true;
      }
      if (selected && ImGui::IsWindowAppearing()) {
        ImGui::SetScrollHereY();
      }
    }
  }
  ImGui::EndChild();
  return changed;
}

/// A slot for one source: a click lists what it can take, and a patch
/// dragged from the browser drops into it.
void render_source_slot(const char *id,
                        const patches::PatchSession &session,
                        const std::vector<EntryDisplay> &entries,
                        PatchLabSource &source, std::string &filter_text,
                        float width) {
  std::string name;
  if (!source.empty() && !resolve_source(session, source, &name, nullptr)) {
    source = {};
  }

  ImGui::PushID(id);
  const ImGuiStyle &style = ImGui::GetStyle();
  const ImVec2 size(width, ImGui::GetTextLineHeight() * 2.0f +
                               style.FramePadding.y * 2.0f +
                               style.ItemInnerSpacing.y);
  const bool clicked = ImGui::InvisibleButton("##slot", size);
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive();

  if (ImGui::BeginDragDropTarget()) {
    const ImGuiPayload *payload = ImGui::GetDragDropPayload();
    if (payload != nullptr && payload->IsDataType(kPatchEntryPayload)) {
      const std::string path(static_cast<const char *>(payload->Data),
                             static_cast<std::size_t>(payload->DataSize));
      if (session.find_entry(path) != nullptr &&
          ImGui::AcceptDragDropPayload(kPatchEntryPayload) != nullptr) {
        source = {.entry_path = path};
      }
    }
    ImGui::EndDragDropTarget();
  }

  const ImGuiID popup_id = ImGui::GetID("##source_list");
  if (clicked) {
    filter_text.clear();
    ImGui::OpenPopupEx(popup_id);
  }
  const bool open = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);

  auto *draw_list = ImGui::GetWindowDrawList();
  const ImGuiCol background = held || open ? ImGuiCol_FrameBgActive
                              : hovered    ? ImGuiCol_FrameBgHovered
                                           : ImGuiCol_FrameBg;
  draw_list->AddRectFilled(min, max, ImGui::GetColorU32(background),
                           style.FrameRounding);
  draw_list->AddRect(min, max, ImGui::GetColorU32(ImGuiCol_Border),
                     style.FrameRounding);

  const ImVec2 inner_min(min.x + style.FramePadding.x,
                         min.y + style.FramePadding.y);
  const ImVec2 inner_max(max.x - style.FramePadding.x,
                         max.y - style.FramePadding.y);
  const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  if (source.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, dim);
    ImGui::RenderTextClipped(inner_min, inner_max, ICON_FA_PLUS, nullptr,
                             nullptr, ImVec2(0.5f, 0.5f));
    ImGui::PopStyleColor();
  } else {
    const float top = (min.y + max.y - ImGui::GetTextLineHeight()) * 0.5f;
    ImGui::RenderTextEllipsis(draw_list, ImVec2(inner_min.x, top), inner_max,
                              inner_max.x, name.c_str(), nullptr, nullptr);
  }
  if (hovered) {
    if (source.empty()) {
      ImGui::SetTooltip("Choose a patch, or drop one here");
    } else {
      ImGui::SetTooltip("%s", name.c_str());
    }
  }

  // Laid out as a combo's list, which opens above when there is no room
  // below. A fixed width keeps the list from narrowing while it is filtered.
  const float list_width =
      std::min(std::max(width, ui::scale::px(320.0f)),
               ImGui::GetMainViewport()->WorkSize.x * 0.6f);
  ImGui::SetNextWindowSizeConstraints(ImVec2(list_width, 0.0f),
                                      ImVec2(list_width, FLT_MAX));
  if (ImGui::BeginComboPopup(popup_id, ImRect(min, max),
                             ImGuiComboFlags_None)) {
    if (render_source_list(session, entries, source, filter_text)) {
      filter_text.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndCombo();
  }
  ImGui::PopID();
}

/// The two patches, side by side.
void render_source_slots(PatchLabContext &context, PatchLabState &state,
                         const std::vector<EntryDisplay> &entries) {
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float width = (ImGui::GetContentRegionAvail().x - spacing) * 0.5f;
  render_source_slot("a", context.session, entries, state.source_a,
                     state.picker_filter, width);
  ImGui::SameLine();
  render_source_slot("b", context.session, entries, state.source_b,
                     state.picker_filter, width);
}

/// Both sources' patches, or why they cannot be had.
std::optional<std::string> load_sources(const PatchLabContext &context,
                                        const PatchLabState &state,
                                        ym2612::Patch &patch_a,
                                        ym2612::Patch &patch_b) {
  if (!resolve_source(context.session, state.source_a, nullptr, &patch_a) ||
      !resolve_source(context.session, state.source_b, nullptr, &patch_b)) {
    return "Failed to load one or both patches.";
  }
  return std::nullopt;
}

void apply_patch_result(PatchLabContext &context,
                        const std::string &history_label,
                        const std::string &history_key,
                        const ym2612::Patch &new_patch) {
  auto &session = context.session;
  context.begin_history(history_label, history_key);

  session.current_patch() = new_patch;
  session.current_patch().name.clear();
  session.set_current_patch_path({});
  session.apply_patch_to_audio();

  context.commit_history();
}

void remember_result(PatchLabState &state, std::string label,
                     const ym2612::Patch &patch) {
  const auto now = std::chrono::system_clock::now();
  const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  std::tm local_time{};
#if defined(_WIN32)
  localtime_s(&local_time, &now_time);
#else
  localtime_r(&now_time, &local_time);
#endif
  char time_text[9]{};
  std::strftime(time_text, sizeof(time_text), "%H:%M:%S", &local_time);

  state.results.insert(state.results.begin(), {.id = state.next_result_id++,
                                               .label = std::move(label),
                                               .created_at = time_text,
                                               .patch = patch});
}

void render_random_section(PatchLabContext &context, PatchLabState &state) {
  ImGui::TextWrapped("Generates a new patch each time.");
  ImGui::Spacing();

  if (ImGui::Button("Apply to current patch")) {
    state.random_template_iterations =
        std::clamp(state.random_template_iterations, 0, 8);
    patch_lab::RandomOptions options;
    options.mode = patch_lab::RandomOptions::Mode::Wild;
    options.mutate_iterations = state.random_template_iterations;

    auto result = patch_lab::random_patch(options);
    const auto key = "patch_lab.random: " + std::to_string(result.seed);
    remember_result(state, "Random", result.patch);
    apply_patch_result(context, "Patch Lab Randomize", key, result.patch);
  }
}

void render_merge_section(PatchLabContext &context, PatchLabState &state,
                          const std::vector<EntryDisplay> &entries) {
  ImGui::TextWrapped(
      "Mixes two patches, taking each parameter from one of them at random.");
  ImGui::Spacing();

  render_source_slots(context, state, entries);

  ImGui::BeginDisabled(state.source_a.empty() || state.source_b.empty());
  if (ImGui::Button("Merge into current patch")) {
    ym2612::Patch patch_a;
    ym2612::Patch patch_b;
    if (auto error = load_sources(context, state, patch_a, patch_b)) {
      state.merge_error = std::move(*error);
    } else {
      auto result = patch_lab::merge(patch_a, patch_b);
      remember_result(state, "Mix", result.patch);
      apply_patch_result(context, "Patch Lab Merge", result.patch.hash(),
                         result.patch);
      state.merge_error.clear();
    }
  }
  ImGui::EndDisabled();

  if (!state.merge_error.empty()) {
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%s",
                       state.merge_error.c_str());
  }
}

void render_morph_section(PatchLabContext &context, PatchLabState &state,
                          const std::vector<EntryDisplay> &entries) {
  ImGui::TextWrapped(
      "Blends between two patches.");
  ImGui::Spacing();

  render_source_slots(context, state, entries);

  ImGui::SliderFloat("Blend", &state.morph_mix, 0.0f, 1.0f);
  ImGui::Checkbox("Interpolate algorithm", &state.morph_interpolate_algorithm);

  ImGui::BeginDisabled(state.source_a.empty() || state.source_b.empty());
  if (ImGui::Button("Morph into current patch")) {
    ym2612::Patch patch_a;
    ym2612::Patch patch_b;
    if (auto error = load_sources(context, state, patch_a, patch_b)) {
      state.morph_error = std::move(*error);
    } else {
      patch_lab::MorphOptions options;
      options.mix = state.morph_mix;
      options.interpolate_algorithm = state.morph_interpolate_algorithm;

      auto result = patch_lab::morph(patch_a, patch_b, options);
      const int percentage =
          static_cast<int>(std::lround(state.morph_mix * 100.0f));
      remember_result(state, "Morph " + std::to_string(percentage) + "%",
                      result.patch);
      apply_patch_result(context, "Patch Lab Morph", result.patch.hash(),
                         result.patch);
      state.morph_error.clear();
    }
  }
  ImGui::EndDisabled();

  if (!state.morph_error.empty()) {
    ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%s",
                       state.morph_error.c_str());
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

  if (ImGui::Button("Mutate Current Patch")) {
    patch_lab::MutateOptions options;
    options.amount = state.mutate_amount;
    options.probability = state.mutate_probability;
    options.allow_algorithm_variation = state.mutate_allow_algorithm_change;

    auto before = context.session.current_patch();
    patch_lab::mutate_in_place(before, options);
    remember_result(state, "Mutate", before);
    apply_patch_result(context, "Patch Lab Mutate", before.hash(), before);
  }
}

void render_result_list(PatchLabContext &context, PatchLabState &state) {
  ImGui::SeparatorText("Results");
  if (state.results.empty()) {
    ImGui::TextDisabled("Generated patches appear here.");
    return;
  }

  for (const auto &result : state.results) {
    ImGui::PushID(static_cast<int>(result.id));
    const std::string display_label = result.label + "  " + result.created_at;
    if (ImGui::Selectable(display_label.c_str())) {
      apply_patch_result(context, "Apply Patch Lab Result",
                         "patch_lab.result:" + std::to_string(result.id),
                         result.patch);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Apply to current patch");
    }
    ImGui::PopID();
  }
}

} // namespace

void render_patch_lab(const char *title, PatchLabContext &context,
                      PatchLabState &state) {
  if (!context.prefs.show_patch_lab) {
    return;
  }

  ImGui::SetNextWindowSize(ui::scale::px(ImVec2(700, 320)),
                           ImGuiCond_FirstUseEver);

  areas::keep_in_area(areas::Area::FooterRight, title);
  if (!ImGui::Begin(title, &context.prefs.show_patch_lab)) {
    ImGui::End();
    return;
  }

  const float available = ImGui::GetContentRegionAvail().x;
  const float left_width = std::max(320.0f, available * 0.62f);
  if (ImGui::BeginChild("##PatchLabControls", ImVec2(left_width, 0), false)) {
    if (ImGui::BeginTabBar("##Patch Lab Operators",
                           ImGuiTabBarFlags_SaveSettings)) {
      auto &repository = context.session.repository();
      if (ImGui::BeginTabItem(ICON_FA_DICE " Random")) {
        render_random_section(context, state);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(ICON_FA_SHUFFLE " Mix")) {
        const auto &entries = current_entries(state, repository);
        render_merge_section(context, state, entries);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(ICON_FA_FLASK_VIAL " Morph")) {
        const auto &entries = current_entries(state, repository);
        render_morph_section(context, state, entries);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem(ICON_FA_VIRUS " Mutate")) {
        render_mutate_section(context, state);
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
  }
  ImGui::EndChild();
  ImGui::SameLine();
  if (ImGui::BeginChild("##PatchLabResults", ImVec2(0, 0), true)) {
    render_result_list(context, state);
  }
  ImGui::EndChild();

  ImGui::End();
}

} // namespace ui
