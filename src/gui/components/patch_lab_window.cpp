#include "patch_lab_window.hpp"
#include "gui/app_window.hpp"

#include "common.hpp"
#include "gui/areas.hpp"
#include "gui/components/centred_glyph.hpp"
#include "gui/components/instrument_icon_picker.hpp"
#include "gui/components/menu_rows.hpp"
#include "gui/components/side_list.hpp"
#include "gui/fonts/megatoy_icons.hpp"
#include "gui/os_file_drop.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "gui/window_title.hpp"
#include "imgui_internal.h"
#include "named_slider.hpp"
#include "patch_selector.hpp"
#include "patches/patch_lab.hpp"
#include "patches/patch_lab_sources.hpp"
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

/// The instrument icon of a browser entry, or "" for none.
std::string_view entry_icon(const patches::PatchEntry *entry) {
  return entry != nullptr && entry->metadata
             ? std::string_view(entry->metadata->icon)
             : std::string_view();
}

/// A row of the source list: the icon, then the label. Drawn only where it
/// shows, as the list holds every patch.
bool icon_selectable(std::string_view icon, const std::string &label,
                     bool selected) {
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float icon_width = ImGui::GetFontSize() * 1.25f;
  const bool clicked = ImGui::Selectable("##row", selected);
  if (!ImGui::IsItemVisible()) {
    return clicked;
  }
  const float line = ImGui::GetTextLineHeight();
  draw_instrument_icon(icon, pos, ImVec2(pos.x + icon_width, pos.y + line));
  ImGui::GetWindowDrawList()->AddText(
      ImVec2(pos.x + icon_width + ImGui::GetStyle().ItemInnerSpacing.x, pos.y),
      ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
  return clicked;
}

/// What a source is called, its icon and what it holds; false when its
/// entry has gone.
bool resolve_source(const patches::PatchSession &session,
                    const PatchLabSource &source, std::string *label,
                    ym2612::Patch *patch, std::string *icon = nullptr) {
  if (source.patch) {
    if (label != nullptr) {
      *label = source.label;
    }
    if (icon != nullptr) {
      *icon = source.icon;
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
  if (icon != nullptr) {
    *icon = entry_icon(entry);
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
                               ICON_MEGATOY_MAGNIFYING_GLASS " Search...",
                               filter_buffer, sizeof(filter_buffer))) {
    filter_text = filter_buffer;
  }
  if (ImGui::IsWindowAppearing()) {
    ImGui::SetKeyboardFocusHere(-1);
  }

  bool changed = false;
  ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f),
                                      ImVec2(FLT_MAX, list_popup_height(16)));
  // Spaced like list rows, a combo's list as any other.
  ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, list_row_spacing());
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
        const std::string_view icon =
            entry_icon(session.document_entry(*item.document));
        if (icon_selectable(icon, item.label, false)) {
          source = {.patch = item.document->patch(),
                    .label = item.label,
                    .icon = std::string(icon)};
          changed = true;
        }
        ImGui::PopID();
        continue;
      }
      const auto &entry = *item.entry->entry;
      const bool selected = source.entry_path == entry.relative_path;
      ImGui::PushID(entry.relative_path.c_str());
      const bool clicked =
          icon_selectable(entry_icon(&entry), item.text(), selected);
      ImGui::PopID();
      if (clicked) {
        source = {.entry_path = entry.relative_path};
        changed = true;
      }
      if (selected && ImGui::IsWindowAppearing()) {
        ImGui::SetScrollHereY();
      }
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  return changed;
}

/// How much larger than the text Patch Lab's large buttons, Generate and the
/// Mix sources, draw their icons.
constexpr float kLargeButtonIconScale = 1.5f;

/// How tall Patch Lab's large buttons stand.
float large_button_height() { return ImGui::GetFrameHeight() * 2.0f; }

/// Where an icon `icon_width` wide, a gap and a name `name_width` wide start
/// so the three stand centred between `left` and `right`.
float centred_start(float left, float right, float icon_width,
                    float name_width) {
  const float gap = ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;
  return std::round((left + right - (icon_width + gap + name_width)) * 0.5f);
}

/// A slot for one source: a click lists what it can take, and a patch
/// dragged from the browser drops into it.
void render_source_slot(const char *id, const PatchLabContext &context,
                        const std::vector<EntryDisplay> &entries,
                        PatchLabSource &source, std::string &filter_text,
                        float width) {
  const patches::PatchSession &session = context.session;
  std::string name;
  std::string icon;
  if (!source.empty() &&
      !resolve_source(session, source, &name, nullptr, &icon)) {
    source = {};
  }

  ImGui::PushID(id);
  const ImGuiStyle &style = ImGui::GetStyle();
  const ImVec2 size(width, large_button_height());
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
    } else if (payload != nullptr &&
               payload->IsDataType(os_file_drop::kPayload) &&
               context.load_dropped_file &&
               ImGui::AcceptDragDropPayload(os_file_drop::kPayload) !=
                   nullptr) {
      // A file from outside: its patch as it is now, the first file of
      // several.
      if (const auto files = os_file_drop::take(); !files.empty()) {
        context.load_dropped_file(
            files.front(), [&source](const ym2612::Patch &patch,
                                     const std::filesystem::path &path) {
              source = {.patch = patch,
                        .label = patch.name.empty() ? path.stem().string()
                                                    : patch.name};
            });
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

  // A frame, as the editor's diagrams are, filled only under the pointer.
  auto *draw_list = ImGui::GetWindowDrawList();
  if (held || open || hovered) {
    draw_list->AddRectFilled(min, max,
                             ImGui::GetColorU32(held
                                                    ? ImGuiCol_FrameBgActive
                                                    : ImGuiCol_FrameBgHovered));
  }
  draw_list->AddRect(min, max,
                     styles::color_u32(styles::MegatoyCol::PreviewBorder));

  // The icon, as large as Generate's, and the name beside it, centred.
  const float icon_width =
      std::round(ImGui::GetFontSize() * kLargeButtonIconScale);
  if (source.empty()) {
    draw_centred_glyph(ICON_MEGATOY_PLUS, min, max,
                       ImGui::GetColorU32(ImGuiCol_TextDisabled),
                       kLargeButtonIconScale);
  } else {
    const float gap = style.ItemInnerSpacing.x * 2.0f;
    const float room =
        std::max(width - style.FramePadding.x * 2.0f - icon_width - gap, 0.0f);
    const float name_width =
        std::min(ImGui::CalcTextSize(name.c_str()).x, room);
    const float left = centred_start(min.x, max.x, icon_width, name_width);
    draw_instrument_icon(icon, ImVec2(left, min.y),
                         ImVec2(left + icon_width, max.y),
                         kLargeButtonIconScale);
    const float text_x = left + icon_width + gap;
    const float top =
        std::round((min.y + max.y - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::RenderTextEllipsis(
        draw_list, ImVec2(text_x, top), ImVec2(text_x + name_width, max.y),
        text_x + name_width, name.c_str(), nullptr, nullptr);
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
  render_source_slot("a", context, entries, state.mix.source_a,
                     state.picker_filter, width);
  ImGui::SameLine();
  render_source_slot("b", context, entries, state.mix.source_b,
                     state.picker_filter, width);
}

/// Both sources' patches, or why they cannot be had.
std::optional<std::string> load_sources(const PatchLabContext &context,
                                        const PatchLabState &state,
                                        ym2612::Patch &patch_a,
                                        ym2612::Patch &patch_b) {
  if (!resolve_source(context.session, state.mix.source_a, nullptr, &patch_a) ||
      !resolve_source(context.session, state.mix.source_b, nullptr, &patch_b)) {
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

/**
 * Keeps a result with the settings it was made with, at the front of the
 * list, as the chosen one. A patch the list already holds moves there rather
 * than being listed twice.
 */
void remember_result(PatchLabState &state, PatchLabState::Page page,
                     std::string settings, const ym2612::Patch &patch) {
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

  const auto hash = patch.hash();
  std::erase_if(state.results, [&](const auto &result) {
    return result.patch.hash() == hash;
  });
  PatchLabState::SessionResult result{.id = state.next_result_id++,
                                      .page = page,
                                      .settings = std::move(settings),
                                      .created_at = time_text,
                                      .patch = patch,
                                      .mutate = state.mutate,
                                      .mix = state.mix};
  state.chosen_result = result.id;
  state.results.insert(state.results.begin(), std::move(result));
}

/// A page's Generate button: twice a button's height across the page, its
/// icon larger and its name beside it, the two centred.
bool generate_button() {
  const bool clicked =
      ImGui::Button("##generate", ImVec2(-FLT_MIN, large_button_height()));
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  const char *label = "Generate";
  const ImVec2 label_size = ImGui::CalcTextSize(label);
  const float icon_width =
      std::round(ImGui::GetFontSize() * kLargeButtonIconScale);
  const float left = centred_start(min.x, max.x, icon_width, label_size.x);
  const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
  draw_centred_glyph(ICON_MEGATOY_REFRESH, ImVec2(left, min.y),
                     ImVec2(left + icon_width, max.y), color,
                     kLargeButtonIconScale);
  ImGui::GetWindowDrawList()->AddText(
      ImVec2(left + icon_width + ImGui::GetStyle().ItemInnerSpacing.x * 2.0f,
             std::round((min.y + max.y - label_size.y) * 0.5f)),
      color, label);
  return clicked;
}

/// A share as a whole percentage.
int percent(float share) {
  return static_cast<int>(std::lround(share * 100.0f));
}

/// A slider over 0 to 1 shown in whole percent, its name inside it.
bool named_percent_slider(const char *label, float &share) {
  int value = percent(share);
  if (!named_slider_int(label, &value, 0, 100, "%d%%")) {
    return false;
  }
  share = static_cast<float>(value) / 100.0f;
  return true;
}

void render_random_section(PatchLabContext &context, PatchLabState &state) {
  ImGui::TextWrapped("Makes a new patch from scratch.");
  ImGui::Spacing();

  if (generate_button()) {
    state.random_template_iterations =
        std::clamp(state.random_template_iterations, 0, 8);
    patch_lab::RandomOptions options;
    options.mode = patch_lab::RandomOptions::Mode::Wild;
    options.mutate_iterations = state.random_template_iterations;

    auto result = patch_lab::random_patch(options);
    const auto key = "patch_lab.random: " + std::to_string(result.seed);
    remember_result(state, PatchLabState::Page::Random, {}, result.patch);
    apply_patch_result(context, "Patch Lab Random", key, result.patch);
  }
}

void render_mutate_section(PatchLabContext &context, PatchLabState &state) {
  ImGui::TextWrapped("Varies the current patch at random.");
  ImGui::Spacing();

  state.mutate.amount = std::clamp(state.mutate.amount, 0, 12);
  state.mutate.probability = std::clamp(state.mutate.probability, 0.0f, 1.0f);

  ImGui::PushItemWidth(-FLT_MIN);
  named_slider_int("Variation Depth", &state.mutate.amount, 0, 12);
  named_percent_slider("Probability", state.mutate.probability);
  ImGui::PopItemWidth();
  ImGui::Checkbox("Allow algorithm change",
                  &state.mutate.allow_algorithm_change);

  if (generate_button()) {
    patch_lab::MutateOptions options;
    options.amount = state.mutate.amount;
    options.probability = state.mutate.probability;
    options.allow_algorithm_variation = state.mutate.allow_algorithm_change;

    auto before = context.session.current_patch();
    patch_lab::mutate_in_place(before, options);
    remember_result(state, PatchLabState::Page::Mutate,
                    "Depth " + std::to_string(state.mutate.amount) + ", " +
                        std::to_string(percent(state.mutate.probability)) + "%",
                    before);
    apply_patch_result(context, "Patch Lab Mutate", before.hash(), before);
  }
}

/// A radio button with a line under it saying what it does.
bool described_radio(const char *label, bool active, const char *description) {
  const bool clicked = ImGui::RadioButton(label, active);
  ImGui::Indent(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
  ImGui::TextColored(styles::color(styles::MegatoyCol::TextMuted), "%s",
                     description);
  ImGui::Unindent(ImGui::GetFrameHeight() +
                  ImGui::GetStyle().ItemInnerSpacing.x);
  return clicked;
}

ym2612::Patch mixed(const PatchLabState &state, const ym2612::Patch &a,
                    const ym2612::Patch &b) {
  if (state.mix.mode == PatchLabState::MixMode::Pick) {
    return patch_lab::merge(a, b).patch;
  }
  patch_lab::MorphOptions options;
  options.mix = state.mix.blend;
  options.interpolate_algorithm = state.mix.interpolate_algorithm;
  return patch_lab::morph(a, b, options).patch;
}

void render_mix_section(PatchLabContext &context, PatchLabState &state,
                        const std::vector<EntryDisplay> &entries) {
  ImGui::TextWrapped("Makes a patch from two others.");
  ImGui::Spacing();

  render_source_slots(context, state, entries);
  ImGui::Spacing();

  using MixMode = PatchLabState::MixMode;
  if (described_radio("Pick", state.mix.mode == MixMode::Pick,
                      "Each parameter from A or B, at random")) {
    state.mix.mode = MixMode::Pick;
  }
  if (described_radio("Blend", state.mix.mode == MixMode::Blend,
                      "In between A and B")) {
    state.mix.mode = MixMode::Blend;
  }
  if (state.mix.mode == MixMode::Blend) {
    const float indent =
        ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::Indent(indent);
    // Its own id: the radio button above is "Blend" too.
    ImGui::SetNextItemWidth(-FLT_MIN);
    named_percent_slider("Blend##amount", state.mix.blend);
    ImGui::Checkbox("Interpolate algorithm", &state.mix.interpolate_algorithm);
    ImGui::Unindent(indent);
  }

  ImGui::BeginDisabled(state.mix.source_a.empty() ||
                       state.mix.source_b.empty());
  if (generate_button()) {
    ym2612::Patch patch_a;
    ym2612::Patch patch_b;
    if (auto error = load_sources(context, state, patch_a, patch_b)) {
      state.mix_error = std::move(*error);
    } else {
      const auto patch = mixed(state, patch_a, patch_b);
      const std::string settings =
          state.mix.mode == MixMode::Pick
              ? "Pick"
              : "Blend " + std::to_string(percent(state.mix.blend)) + "%";
      remember_result(state, PatchLabState::Page::Mix, settings, patch);
      apply_patch_result(context, "Patch Lab Mix", patch.hash(), patch);
      state.mix_error.clear();
    }
  }
  ImGui::EndDisabled();

  if (!state.mix_error.empty()) {
    ImGui::TextColored(styles::color(styles::MegatoyCol::TextMuted), "%s",
                       state.mix_error.c_str());
  }
}

/// The pages' names and icons, in the order the list shows them.
constexpr const char *kPageIcons[] = {ICON_MEGATOY_DICE, ICON_MEGATOY_VIRUS,
                                      ICON_MEGATOY_SHUFFLE};
constexpr const char *kPageNames[] = {"Random", "Mutate", "Mix"};

/// Puts back the page and settings `result` was made with, and its patch.
void choose_result(PatchLabContext &context, PatchLabState &state,
                   const PatchLabState::SessionResult &result) {
  state.page = result.page;
  state.chosen_result = result.id;
  if (result.page == PatchLabState::Page::Mutate) {
    state.mutate = result.mutate;
  } else if (result.page == PatchLabState::Page::Mix) {
    state.mix = result.mix;
  }
  apply_patch_result(context, "Apply Patch Lab Result",
                     "patch_lab.result:" + std::to_string(result.id),
                     result.patch);
}

/// How wide the list down the left is: room for a page's name, and for a
/// result's settings at their longest.
float lab_list_width() {
  const ImGuiStyle &style = ImGui::GetStyle();
  float text = ImGui::CalcTextSize("Depth 12, 100%").x;
  for (std::size_t i = 0; i < std::size(kPageNames); ++i) {
    text = std::max(text, ImGui::CalcTextSize(kPageNames[i]).x);
  }
  return ImGui::GetFontSize() + style.ItemInnerSpacing.x + text +
         style.FramePadding.x * 2.0f + style.WindowPadding.x * 2.0f;
}

/// A row of the list: the icon, muted where `muted_icon`, then a line of
/// text, and under it a second one, muted, when there is one.
bool lab_list_row(const char *icon, bool muted_icon, const char *text,
                  const char *muted_text, bool selected) {
  const ImGuiStyle &style = ImGui::GetStyle();
  const float line = ImGui::GetTextLineHeight();
  const float gap = ui::scale::px(2.0f);
  const ImVec2 row = ImGui::GetCursorScreenPos();
  const bool two_lines = text != nullptr && muted_text != nullptr;
  const bool clicked = ImGui::Selectable(
      "##row", selected, 0, ImVec2(0.0f, two_lines ? line * 2.0f + gap : line));
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const float inset = style.FramePadding.x;
  const ImU32 text_color = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 muted_color =
      ImGui::GetColorU32(styles::color(styles::MegatoyCol::TextMuted));
  draw_list->AddText(ImVec2(row.x + inset, row.y),
                     muted_icon ? muted_color : text_color, icon);
  const float text_x =
      row.x + inset + ImGui::GetFontSize() + style.ItemInnerSpacing.x;
  if (text != nullptr) {
    draw_list->AddText(ImVec2(text_x, row.y), text_color, text);
  }
  if (muted_text != nullptr) {
    draw_list->AddText(ImVec2(text_x, two_lines ? row.y + line + gap : row.y),
                       muted_color, muted_text);
  }
  return clicked;
}

/// The pages down the left, and under them the results made on them, each
/// with its page's icon, what it was made with and when; choosing one puts
/// back its page and settings.
void render_lab_list(PatchLabContext &context, PatchLabState &state) {
  if (begin_side_list("##patch_lab_list", lab_list_width())) {
    begin_side_list_rows();
    // The pages and the results each in their own ID scope: a result's id
    // can be a page's index.
    ImGui::PushID("pages");
    for (std::size_t i = 0; i < std::size(kPageNames); ++i) {
      const auto page = static_cast<PatchLabState::Page>(i);
      ImGui::PushID(static_cast<int>(i));
      if (lab_list_row(kPageIcons[i], false, kPageNames[i], nullptr,
                       state.page == page && !state.chosen_result)) {
        state.page = page;
        state.chosen_result.reset();
      }
      ImGui::PopID();
    }
    ImGui::PopID();
    end_side_list_rows();

    ImGui::SeparatorText("Results");
    begin_side_list_rows();
    if (state.results.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text,
                            styles::color(styles::MegatoyCol::TextMuted));
      ImGui::TextWrapped("Generated patches appear here.");
      ImGui::PopStyleColor();
    }
    const PatchLabState::SessionResult *chosen = nullptr;
    ImGui::PushID("results");
    for (const auto &result : state.results) {
      ImGui::PushID(static_cast<int>(result.id));
      const bool has_settings = !result.settings.empty();
      // A result's icon is muted, so it stands apart from the pages'.
      if (lab_list_row(kPageIcons[static_cast<std::size_t>(result.page)], true,
                       has_settings ? result.settings.c_str() : nullptr,
                       result.created_at.c_str(),
                       state.chosen_result == result.id)) {
        chosen = &result;
      }
      ImGui::PopID();
    }
    ImGui::PopID();
    end_side_list_rows();
    if (chosen != nullptr) {
      choose_result(context, state, *chosen);
    }
  }
  end_side_list();
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
  if (!ui::begin_window(title, &context.prefs.show_patch_lab)) {
    ImGui::End();
    return;
  }

  render_lab_list(context, state);
  // One child per page, so each keeps its own scroll.
  ImGui::PushID(static_cast<int>(state.page));
  if (ImGui::BeginChild("##PatchLabControls", ImVec2(0, 0), false)) {
    switch (state.page) {
    case PatchLabState::Page::Random:
      render_random_section(context, state);
      break;
    case PatchLabState::Page::Mutate:
      render_mutate_section(context, state);
      break;
    case PatchLabState::Page::Mix:
      render_mix_section(context, state,
                         current_entries(state, context.session.repository()));
      break;
    }
  }
  ImGui::EndChild();
  ImGui::PopID();

  ImGui::End();
}

} // namespace ui
