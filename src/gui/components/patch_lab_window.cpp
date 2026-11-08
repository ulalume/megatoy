#include "patch_lab_window.hpp"
#include "gui/app_window.hpp"

#include "common.hpp"
#include "gui/areas.hpp"
#include "gui/components/instrument_icon_picker.hpp"
#include "gui/components/menu_rows.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "gui/window_title.hpp"
#include "imgui_internal.h"
#include "named_slider.hpp"
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

/// The instrument icon of a browser entry, or "" for none.
std::string entry_icon(const patches::PatchEntry *entry) {
  return entry != nullptr && entry->metadata ? entry->metadata->icon
                                             : std::string();
}

/// A row of the source list: the icon, then the label.
bool icon_selectable(const std::string &icon, const std::string &label,
                     bool selected) {
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float icon_width = ImGui::GetFontSize() * 1.25f;
  const bool clicked = ImGui::Selectable("##row", selected);
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
                               ICON_FA_MAGNIFYING_GLASS " Search...",
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
        const std::string icon =
            entry_icon(session.document_entry(*item.document));
        if (icon_selectable(icon, item.label, false)) {
          source = {.patch = item.document->patch(),
                    .label = item.label,
                    .icon = icon};
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

/// How much larger than the text a slot draws its patch's icon.
constexpr float kSlotIconScale = 1.75f;

/// A slot for one source: a click lists what it can take, and a patch
/// dragged from the browser drops into it.
void render_source_slot(const char *id,
                        const patches::PatchSession &session,
                        const std::vector<EntryDisplay> &entries,
                        PatchLabSource &source, std::string &filter_text,
                        float width) {
  std::string name;
  std::string icon;
  if (!source.empty() &&
      !resolve_source(session, source, &name, nullptr, &icon)) {
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
    // The icon at the left, as large as the slot's two lines allow.
    const float icon_size = inner_max.y - inner_min.y;
    draw_instrument_icon(icon, inner_min,
                         ImVec2(inner_min.x + icon_size, inner_max.y),
                         kSlotIconScale);
    const float text_x = inner_min.x + icon_size + style.ItemInnerSpacing.x;
    const float top = (min.y + max.y - ImGui::GetTextLineHeight()) * 0.5f;
    ImGui::RenderTextEllipsis(draw_list, ImVec2(text_x, top), inner_max,
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
  PatchLabState::SessionResult result{
      .id = state.next_result_id++,
      .page = page,
      .settings = std::move(settings),
      .created_at = time_text,
      .patch = patch,
      .mutate_amount = state.mutate_amount,
      .mutate_probability = state.mutate_probability,
      .mutate_allow_algorithm_change = state.mutate_allow_algorithm_change,
      .mix_mode = state.mix_mode,
      .blend = state.blend,
      .blend_interpolate_algorithm = state.blend_interpolate_algorithm,
      .source_a = state.source_a,
      .source_b = state.source_b};
  state.chosen_result = result.id;
  state.results.insert(state.results.begin(), std::move(result));
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

  if (ImGui::Button(ICON_FA_ARROWS_ROTATE " Generate")) {
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

  state.mutate_amount = std::clamp(state.mutate_amount, 0, 12);
  state.mutate_probability = std::clamp(state.mutate_probability, 0.0f, 1.0f);

  ImGui::PushItemWidth(-FLT_MIN);
  named_slider_int("Variation Depth", &state.mutate_amount, 0, 12);
  named_percent_slider("Probability", state.mutate_probability);
  ImGui::PopItemWidth();
  ImGui::Checkbox("Allow algorithm change",
                  &state.mutate_allow_algorithm_change);

  if (ImGui::Button(ICON_FA_ARROWS_ROTATE " Generate")) {
    patch_lab::MutateOptions options;
    options.amount = state.mutate_amount;
    options.probability = state.mutate_probability;
    options.allow_algorithm_variation = state.mutate_allow_algorithm_change;

    auto before = context.session.current_patch();
    patch_lab::mutate_in_place(before, options);
    remember_result(state, PatchLabState::Page::Mutate,
                    "Depth " + std::to_string(state.mutate_amount) + ", " +
                        std::to_string(percent(state.mutate_probability)) + "%",
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
  if (state.mix_mode == PatchLabState::MixMode::Pick) {
    return patch_lab::merge(a, b).patch;
  }
  patch_lab::MorphOptions options;
  options.mix = state.blend;
  options.interpolate_algorithm = state.blend_interpolate_algorithm;
  return patch_lab::morph(a, b, options).patch;
}

void render_mix_section(PatchLabContext &context, PatchLabState &state,
                        const std::vector<EntryDisplay> &entries) {
  ImGui::TextWrapped("Makes a patch from two others.");
  ImGui::Spacing();

  render_source_slots(context, state, entries);
  ImGui::Spacing();

  using MixMode = PatchLabState::MixMode;
  if (described_radio("Pick", state.mix_mode == MixMode::Pick,
                      "Each parameter from A or B, at random")) {
    state.mix_mode = MixMode::Pick;
  }
  if (described_radio("Blend", state.mix_mode == MixMode::Blend,
                      "In between A and B")) {
    state.mix_mode = MixMode::Blend;
  }
  if (state.mix_mode == MixMode::Blend) {
    const float indent =
        ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
    ImGui::Indent(indent);
    // Its own id: the radio button above is "Blend" too.
    ImGui::SetNextItemWidth(-FLT_MIN);
    named_percent_slider("Blend##amount", state.blend);
    ImGui::Checkbox("Interpolate algorithm",
                    &state.blend_interpolate_algorithm);
    ImGui::Unindent(indent);
  }

  ImGui::BeginDisabled(state.source_a.empty() || state.source_b.empty());
  if (ImGui::Button(ICON_FA_ARROWS_ROTATE " Generate")) {
    ym2612::Patch patch_a;
    ym2612::Patch patch_b;
    if (auto error = load_sources(context, state, patch_a, patch_b)) {
      state.mix_error = std::move(*error);
    } else {
      const auto patch = mixed(state, patch_a, patch_b);
      const std::string settings =
          state.mix_mode == MixMode::Pick
              ? "Pick"
              : "Blend " + std::to_string(percent(state.blend)) + "%";
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
constexpr const char *kPageIcons[] = {ICON_FA_DICE, ICON_FA_VIRUS,
                                      ICON_FA_SHUFFLE};
constexpr const char *kPageNames[] = {"Random", "Mutate", "Mix"};

/// Puts back the page and settings `result` was made with, and its patch.
void choose_result(PatchLabContext &context, PatchLabState &state,
                   const PatchLabState::SessionResult &result) {
  state.page = result.page;
  state.chosen_result = result.id;
  if (result.page == PatchLabState::Page::Mutate) {
    state.mutate_amount = result.mutate_amount;
    state.mutate_probability = result.mutate_probability;
    state.mutate_allow_algorithm_change = result.mutate_allow_algorithm_change;
  } else if (result.page == PatchLabState::Page::Mix) {
    state.mix_mode = result.mix_mode;
    state.blend = result.blend;
    state.blend_interpolate_algorithm = result.blend_interpolate_algorithm;
    state.source_a = result.source_a;
    state.source_b = result.source_b;
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

/// A row of the list: the icon, then a line of text, and under it a second
/// one, muted, when there is one.
bool lab_list_row(const char *icon, const char *text, const char *muted_text,
                  bool selected) {
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
  draw_list->AddText(ImVec2(row.x + inset, row.y), text_color, icon);
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
  if (ImGui::BeginChild("##patch_lab_list", ImVec2(lab_list_width(), 0.0f))) {
    ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, list_row_spacing());
    // A row's highlight reaches half the spacing above it, the first one's
    // too.
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + list_row_spacing() * 0.5f);
    // The pages and the results each in their own ID scope: a result's id
    // can be a page's index.
    ImGui::PushID("pages");
    for (std::size_t i = 0; i < std::size(kPageNames); ++i) {
      const auto page = static_cast<PatchLabState::Page>(i);
      ImGui::PushID(static_cast<int>(i));
      if (lab_list_row(kPageIcons[i], kPageNames[i], nullptr,
                       state.page == page && !state.chosen_result)) {
        state.page = page;
        state.chosen_result.reset();
      }
      ImGui::PopID();
    }
    ImGui::PopID();
    ImGui::PopStyleVar();

    ImGui::SeparatorText("Results");
    ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, list_row_spacing());
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + list_row_spacing() * 0.5f);
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
      if (lab_list_row(kPageIcons[static_cast<std::size_t>(result.page)],
                       has_settings ? result.settings.c_str() : nullptr,
                       result.created_at.c_str(),
                       state.chosen_result == result.id)) {
        chosen = &result;
      }
      ImGui::PopID();
    }
    ImGui::PopID();
    ImGui::PopStyleVar();
    if (chosen != nullptr) {
      choose_result(context, state, *chosen);
    }
  }
  ImGui::EndChild();

  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const float x = ImGui::GetItemRectMax().x + gap;
  ImGui::GetWindowDrawList()->AddLine(ImVec2(x, ImGui::GetItemRectMin().y),
                                      ImVec2(x, ImGui::GetItemRectMax().y),
                                      ImGui::GetColorU32(ImGuiCol_Separator));
  ImGui::SameLine(0.0f, gap * 2.0f);
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
