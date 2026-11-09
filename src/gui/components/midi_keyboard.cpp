#include "midi_keyboard.hpp"
#include "gui/app_window.hpp"

#include "core/types.hpp"
#include "gui/areas.hpp"
#include "gui/components/menu_rows.hpp"
#include "gui/fonts/megatoy_icons.hpp"
#include "gui/frame_requests.hpp"
#include "gui/input/key_name_utils.hpp"
#include "gui/input/keyboard_typing.hpp"
#include "gui/input/touch.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "util.hpp"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>
#include <optional>
#include <string>
#include <vector>

namespace ui {
namespace {

constexpr float playback_step_duration = 0.35f;

/// The chromatic keyboard's narrowest white key, and its black keys' width
/// and height against a white key's.
constexpr float kMinWhiteKeyWidth = 24.0f;
constexpr float kBlackKeyWidth = 0.6f;
constexpr float kBlackKeyHeight = 0.6f;

std::vector<ym2612::Note>
build_playback_sequence(const std::vector<Key> &scale_keys,
                        uint8_t base_octave) {
  std::vector<ym2612::Note> sequence;
  if (scale_keys.empty()) {
    return sequence;
  }
  std::vector<Key> keys_with_top = scale_keys;
  keys_with_top.push_back(scale_keys.front());
  int current_octave = std::clamp<int>(base_octave, 0, 7);
  int prev_key_value = -1;
  for (const auto key : keys_with_top) {
    const int key_value = static_cast<int>(key);
    if (prev_key_value >= 0 && key_value < prev_key_value) {
      current_octave = std::min(current_octave + 1, 7);
    }
    sequence.push_back(ym2612::Note{
        static_cast<uint8_t>(std::clamp(current_octave, 0, 7)), key});
    prev_key_value = key_value;
  }
  return sequence;
}

/// Lets go of the keys the mouse and fingers hold, for a frame the keys are
/// not drawn in and so cannot see them lift.
void release_held_keys(MidiKeyboardContext &context) {
  for (const auto &note : context.state.held_notes) {
    if (context.key_off) {
      context.key_off(note);
    }
  }
  context.state.held_notes.clear();
  context.state.fingers_on_keyboard.clear();
}

void stop_playback(MidiKeyboardContext &context) {
  auto &playback = context.state.playback;
  if (playback.note_active && context.key_off) {
    context.key_off(playback.current_note);
  }
  playback.note_active = false;
  playback.playing = false;
  playback.timer = 0.0f;
  playback.next_index = 0;
  playback.sequence_snapshot.clear();
}

bool can_play_scale(const MidiKeyboardContext &context) {
  return !context.state.playback_sequence.empty() && context.key_on &&
         context.key_off;
}

/// The scale the keyboard is set to, as "C Major" or "Chromatic".
std::string scale_title(const InputState &input) {
  const auto &settings = input.midi_keyboard_settings;
  const char *scale = scale_names[static_cast<int>(settings.scale)];
  if (settings.scale == Scale::CHROMATIC) {
    return scale;
  }
  return std::string(key_names[static_cast<int>(settings.key)]) + " " + scale;
}

void start_playback(MidiKeyboardContext &context) {
  if (!can_play_scale(context)) {
    return;
  }
  stop_playback(context);
  context.state.playback.sequence_snapshot = context.state.playback_sequence;
  context.state.playback.playing = true;
  context.state.playback.timer = 0.0f;
  context.state.playback.next_index = 0;
}

void update_playback(MidiKeyboardContext &context, float delta_time) {
  auto &playback = context.state.playback;
  const auto &sequence = playback.sequence_snapshot;
  if (!playback.playing) {
    return;
  }
  if (sequence.empty() || !context.key_on || !context.key_off) {
    stop_playback(context);
    return;
  }
  // The next note is due on time, not at the next input.
  keep_drawing();

  playback.timer -= delta_time;
  if (playback.timer > 0.0f) {
    return;
  }

  if (playback.note_active) {
    context.key_off(playback.current_note);
    playback.note_active = false;
  }

  const ym2612::Note &next_note = sequence[playback.next_index];
  context.key_on(next_note, 127);
  playback.current_note = next_note;
  playback.note_active = true;

  playback.next_index = (playback.next_index + 1) % sequence.size();
  playback.timer += playback_step_duration;
  if (playback.timer < 0.0f) {
    playback.timer = playback_step_duration;
  }
}

bool refresh_state(MidiKeyboardState &state, const InputState &input,
                   TypingKeyboardLayout layout,
                   const TypingLayout &custom_layout) {
  const auto &settings = input.midi_keyboard_settings;
  const bool custom_layout_changed =
      layout == TypingKeyboardLayout::Custom &&
      state.cached_custom_layout != custom_layout;
  if (state.initialized && state.cached_scale == settings.scale &&
      state.cached_key == settings.key &&
      state.cached_octave == input.keyboard_typing_octave &&
      state.cached_layout == layout && !custom_layout_changed) {
    return false;
  }

  state.cached_scale = settings.scale;
  state.cached_key = settings.key;
  state.cached_octave = input.keyboard_typing_octave;
  state.cached_layout = layout;
  state.cached_custom_layout = custom_layout;

  state.key_mappings =
      create_key_mappings(settings.scale, settings.key,
                          input.keyboard_typing_octave, layout, custom_layout);

  state.reverse_mappings.clear();
  for (const auto &pair : state.key_mappings) {
    state.reverse_mappings[pair.second] = pair.first;
  }

  state.scale_keys = keys_from_scale_and_key(settings.scale, settings.key);

  state.display_notes.clear();
  for (int midi_note = ui::midi_note_start; midi_note <= ui::midi_note_end;
       ++midi_note) {
    ym2612::Note note = ym2612::Note::from_midi_note(midi_note);
    if (std::find(state.scale_keys.begin(), state.scale_keys.end(), note.key) !=
        state.scale_keys.end()) {
      state.display_notes.push_back(note);
    }
  }

  if (state.key_mappings.empty()) {
    state.typing_range_label.clear();
  } else {
    auto cmp = [](const auto &lhs, const auto &rhs) {
      return lhs.second.midi_note() < rhs.second.midi_note();
    };
    const auto range = std::minmax_element(state.key_mappings.begin(),
                                           state.key_mappings.end(), cmp);
    if (range.first != state.key_mappings.end() &&
        range.second != state.key_mappings.end()) {
      state.typing_range_label =
          range.first->second.name() + "-" + range.second->second.name();
    } else {
      state.typing_range_label.clear();
    }
  }

  state.playback_sequence =
      build_playback_sequence(state.scale_keys, input.keyboard_typing_octave);
  state.typing_range_moved = true;
  state.initialized = true;
  return true;
}

const char *typing_label(const MidiKeyboardState &state) {
  return state.typing_range_label.empty() ? "Keys"
                                          : state.typing_range_label.c_str();
}

/**
 * The operators' switches, OP1 to OP4, as megamml's keyboard has them: lit
 * while the operator sounds, an outline while it is muted. How the patch is
 * played, not the patch (ym2612/playback.hpp).
 */
void render_operator_switches(std::array<bool, 4> &enabled) {
  const ImGuiStyle &style = ImGui::GetStyle();
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(styles::color(styles::MegatoyCol::TextMuted), "OP");
  const float size = ImGui::GetFrameHeight();
  for (std::size_t i = 0; i < enabled.size(); ++i) {
    ImGui::SameLine(0.0f,
                    i == 0 ? style.ItemInnerSpacing.x : ui::scale::px(1.0f));
    const bool on = enabled[i];
    if (!on) {
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_Text,
                            styles::color(styles::MegatoyCol::TextMuted));
      ImGui::PushStyleColor(ImGuiCol_Border,
                            styles::color(styles::MegatoyCol::EnvelopeGrid));
      ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, ui::scale::px(1.0f));
    }
    const char label[] = {static_cast<char>('1' + i), '\0'};
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::Button(label, ImVec2(size, size))) {
      enabled[i] = !on;
    }
    ImGui::PopID();
    if (!on) {
      ImGui::PopStyleVar();
      ImGui::PopStyleColor(3);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("OP%d", static_cast<int>(i) + 1);
    }
  }
}

std::pair<ImGuiKey, ImGuiKey>
octave_keys_for_layout(TypingKeyboardLayout layout,
                       const PreferenceManager::UIPreferences &prefs) {
  const auto defaults = keyboard_octave_keys();
  if (layout != TypingKeyboardLayout::Custom) {
    return defaults;
  }
  const ImGuiKey down =
      key_from_preference(prefs.custom_typing_octave_down_key, defaults.first);
  const ImGuiKey up =
      key_from_preference(prefs.custom_typing_octave_up_key, defaults.second);
  return {down, up};
}

} // namespace

void render_midi_keyboard(const char *title, MidiKeyboardContext &context) {
  const ImU32 black_key_col =
      styles::color_u32(styles::MegatoyCol::PianoBlackKey);
  const ImU32 black_key_pressed_col =
      styles::color_u32(styles::MegatoyCol::PianoBlackKeyPressed);
  const ImU32 white_key_col =
      styles::color_u32(styles::MegatoyCol::PianoWhiteKey);
  const ImU32 white_key_pressed_col =
      styles::color_u32(styles::MegatoyCol::PianoWhiteKeyPressed);
  const ImU32 white_key_text_col =
      styles::color_u32(styles::MegatoyCol::TextOnWhiteKey);
  const ImU32 black_key_text_col =
      styles::color_u32(styles::MegatoyCol::TextOnBlackKey);
  const ImU32 key_border_col =
      styles::color_u32(styles::MegatoyCol::PianoKeyBorder);

  auto &input = context.input_state;
  auto &keyboard_settings = input.midi_keyboard_settings;
  auto &ui_prefs = context.ui_prefs;
  const auto typing_layout = clamp_layout_pref(ui_prefs.midi_keyboard_layout);
  ui_prefs.midi_keyboard_layout = static_cast<int>(typing_layout);
  const TypingLayout custom_layout =
      layout_from_preferences(ui_prefs.custom_typing_layout_keys);
  const auto octave_keys = octave_keys_for_layout(typing_layout, ui_prefs);

  const auto clamp_scale = [](int value) {
    return std::clamp(value, 0, static_cast<int>(Scale::RYUKYU));
  };
  const auto clamp_key = [](int value) {
    return std::clamp(value, 0, static_cast<int>(Key::B));
  };
  const Scale pref_scale =
      static_cast<Scale>(clamp_scale(context.ui_prefs.midi_keyboard_scale));
  const Key pref_key =
      static_cast<Key>(clamp_key(context.ui_prefs.midi_keyboard_key));
  const int pref_octave =
      std::clamp(context.ui_prefs.midi_keyboard_typing_octave, 0, 7);

  if (keyboard_settings.scale != pref_scale) {
    keyboard_settings.scale = pref_scale;
  }
  if (keyboard_settings.key != pref_key) {
    keyboard_settings.key = pref_key;
  }
  if (input.keyboard_typing_octave != pref_octave) {
    input.keyboard_typing_octave = static_cast<uint8_t>(pref_octave);
  }

  refresh_state(context.state, input, typing_layout, custom_layout);
  const auto &key_mappings = context.state.key_mappings;

  update_playback(context, ImGui::GetIO().DeltaTime);

  const ImGuiIO &io = ImGui::GetIO();
  const bool primary_modifier = io.KeyCtrl || io.KeySuper;
  if (!primary_modifier && !ImGui::GetIO().WantTextInput &&
      !key_mappings.empty()) {
    KeyboardTypingContext typing_context{
        context.input_state, octave_keys.first, octave_keys.second,
        [&context](ym2612::Note note, uint8_t velocity) {
          context.key_on(note, velocity);
        },
        [&context](ym2612::Note note) { context.key_off(note); }};
    check_keyboard_typing(typing_context, key_mappings);
  }
  context.ui_prefs.midi_keyboard_typing_octave =
      static_cast<int>(input.keyboard_typing_octave);

  if (!ui_prefs.show_midi_keyboard) {
    release_held_keys(context);
    return;
  }

  ImGui::SetNextWindowSize(ui::scale::px(ImVec2(400, 180)),
                           ImGuiCond_FirstUseEver);
  areas::keep_in_area(areas::Area::FooterRight, title);
  if (!ui::begin_window(title, &ui_prefs.show_midi_keyboard)) {
    release_held_keys(context);
    ImGui::End();
    return;
  }

  // Key and scale read as one phrase, "C Major Scale".
  ImGui::BeginDisabled(keyboard_settings.scale == Scale::CHROMATIC);
  int current_key = static_cast<int>(keyboard_settings.key);
  ImGui::SetNextItemWidth(ui::scale::px(40.0f));
  if (ui::combo("##Key", &current_key, key_names, IM_ARRAYSIZE(key_names))) {
    keyboard_settings.key = static_cast<Key>(current_key);
    context.ui_prefs.midi_keyboard_key = current_key;
    refresh_state(context.state, input, typing_layout, custom_layout);
  }
  ImGui::EndDisabled();

  ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
  int current_scale = static_cast<int>(keyboard_settings.scale);
  ImGui::SetNextItemWidth(ui::scale::px(150.0f));
  if (ui::combo("##Scale", &current_scale, scale_names,
                IM_ARRAYSIZE(scale_names))) {
    keyboard_settings.scale = static_cast<Scale>(current_scale);
    context.ui_prefs.midi_keyboard_scale = current_scale;
    if (keyboard_settings.scale == Scale::CHROMATIC) {
      keyboard_settings.key = Key::C;
      context.ui_prefs.midi_keyboard_key = static_cast<int>(Key::C);
    }
    refresh_state(context.state, input, typing_layout, custom_layout);
  }

  ImGui::SameLine();

  render_operator_switches(context.operator_enable);

  // The slider takes the rest of the row, or a row of its own when too
  // little is left.
  ImGui::SameLine();
  if (ImGui::GetContentRegionAvail().x < ui::scale::px(120.0f)) {
    ImGui::NewLine();
  }
  int current_key_octave = static_cast<int>(input.keyboard_typing_octave);
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
  if (ImGui::SliderInt("##Typing", &current_key_octave, 0, 7,
                       typing_label(context.state))) {
    input.keyboard_typing_octave = current_key_octave;
    context.ui_prefs.midi_keyboard_typing_octave = current_key_octave;
    refresh_state(context.state, input, typing_layout, custom_layout);
  }
  if (ImGui::IsItemHovered()) {
    const auto key_label = [](ImGuiKey key) {
      if (key == ImGuiKey_None) {
        return std::string("Unassigned");
      }
      std::string label = short_key_name(key);
      if (label.empty() || label == "??") {
        const char *fallback = ImGui::GetKeyName(key);
        label = (fallback && fallback[0] != '\0') ? fallback : "Unknown";
      }
      return label;
    };
    const std::string tooltip =
        "Typing\nOctave Down: " + key_label(octave_keys.first) +
        "\nOctave Up: " + key_label(octave_keys.second);
    ImGui::SetTooltip("%s", tooltip.c_str());
  }

  // Scrollable keyboard area
  const ImVec2 available_region = ImGui::GetContentRegionAvail();

  if (ImGui::BeginChild("KeyboardScroll", ImVec2(0, available_region.y), false,
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    const auto &notes = context.state.display_notes;
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float key_height = ImGui::GetContentRegionAvail().y;
    const float y = cursor.y;

    struct KeyRect {
      ImRect rect;
      ym2612::Note note;
      bool white;
    };
    std::vector<KeyRect> key_rects;
    key_rects.reserve(notes.size());
    // The chromatic scale is laid out as a piano's: white keys side by side,
    // black keys shorter and narrower over the line between two. Any other
    // scale lays its notes side by side alike.
    const bool piano = keyboard_settings.scale == Scale::CHROMATIC;
    if (piano) {
      const auto whites = static_cast<float>(
          std::count_if(notes.begin(), notes.end(), [](const auto &note) {
            return ui::is_white_key(note.key);
          }));
      const bool black_first =
          !notes.empty() && !ui::is_white_key(notes.front().key);
      const bool black_last =
          !notes.empty() && !ui::is_white_key(notes.back().key);
      // A black key at either end stands half out past the white keys.
      const float ends =
          (black_first ? 0.5f : 0.0f) + (black_last ? 0.5f : 0.0f);
      const float min_white_width = ui::scale::px(kMinWhiteKeyWidth);
      // One pixel kept for the last key's right border.
      const float white_width =
          whites == 0.0f
              ? min_white_width
              : std::max(min_white_width, (available_region.x - 1.0f) /
                                              (whites + ends * kBlackKeyWidth));
      const float black_width = std::round(white_width * kBlackKeyWidth);
      const float black_height = std::round(key_height * kBlackKeyHeight);
      float x = cursor.x + (black_first ? black_width * 0.5f : 0.0f);
      for (const auto &note : notes) {
        if (ui::is_white_key(note.key)) {
          key_rects.push_back(
              {ImRect(std::floor(x), y, std::floor(x + white_width),
                      y + key_height),
               note, true});
          x += white_width;
        } else {
          const float left = std::floor(x - black_width * 0.5f);
          key_rects.push_back(
              {ImRect(left, y, left + black_width, y + black_height), note,
               false});
        }
      }
    } else {
      const float min_key_width = ui::scale::px(18.0f);
      // One pixel kept for the last key's right border.
      const float key_width =
          notes.empty() ? min_key_width
                        : std::max(min_key_width,
                                   (available_region.x - 1.0f) / notes.size());
      float x = cursor.x;
      for (const auto &note : notes) {
        // On whole pixels, so the borders do not blur across two.
        key_rects.push_back({ImRect(std::floor(x), y, std::floor(x + key_width),
                                    y + key_height),
                             note, ui::is_white_key(note.key)});
        x += key_width;
      }
    }
    // Black keys lie over white ones, so they take the pointer first.
    std::stable_partition(
        key_rects.begin(), key_rects.end(),
        [&](const KeyRect &key) { return !key.white && piano; });

    // A press on any key sounds whichever key the pointer is over, so a drag
    // plays each key it crosses. On a touch screen the fingers play, each its
    // own key, and the mouse SDL makes of the first one is left out.
    const bool by_touch =
        ImGui::GetIO().MouseSource == ImGuiMouseSource_TouchScreen;
    bool pointer_held = false;
    std::optional<ym2612::Note> under_pointer;
    for (const auto &key : key_rects) {
      ImGui::SetCursorScreenPos(key.rect.Min);
      ImGui::PushID(key.note.midi_note());
      ImGui::InvisibleButton("key", key.rect.GetSize());
      pointer_held |= ImGui::IsItemActive() && !by_touch;
      if (!under_pointer &&
          ImGui::IsItemHovered(
              ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        under_pointer = key.note;
      }
      ImGui::PopID();
    }

    // Where the keys typing plays begin and end.
    float typed_min = FLT_MAX;
    float typed_max = -FLT_MAX;
    // Drawn white keys first, so the black ones lie over them.
    for (const bool white_pass : {true, false}) {
      for (const auto &key : key_rects) {
        if (key.white != white_pass) {
          continue;
        }
        const auto &note = key.note;
        const ImVec2 key_min = key.rect.Min;
        const ImVec2 key_max = key.rect.Max;
        const float key_width = key_max.x - key_min.x;
        const bool is_white = key.white;
        const bool is_pressed =
            context.key_is_pressed ? context.key_is_pressed(note) : false;
        const ImU32 col =
            is_white ? (is_pressed ? white_key_pressed_col : white_key_col)
                     : (is_pressed ? black_key_pressed_col : black_key_col);
        if (!is_white && piano) {
          // Over the white keys, so the colour, translucent, goes on the
          // window's background rather than on them.
          draw_list->AddRectFilled(key_min, key_max,
                                   ImGui::GetColorU32(ImGuiCol_WindowBg));
        }
        draw_list->AddRectFilled(key_min, key_max, col);
        // One pixel further right, onto the next key's left edge, where that
        // key's own border draws over it again: neighbours share one line.
        draw_list->AddRect(key_min, ImVec2(key_max.x + 1.0f, key_max.y),
                           key_border_col, 0, 0, 1.0f);

        const ImU32 text_color =
            is_white ? white_key_text_col : black_key_text_col;
        const auto mapped = context.state.reverse_mappings.find(note);
        // How far down the key's typing name reaches, to keep its note name
        // off it.
        float key_name_bottom = key_min.y;
        if (mapped != context.state.reverse_mappings.end()) {
          typed_min = std::min(typed_min, key_min.x);
          typed_max = std::max(typed_max, key_max.x);
          // The key's name a little below the bar over the keys typing
          // plays: below the black keys on a white one that has them beside
          // it.
          const ImVec2 bar_max(key_max.x, key_min.y + ui::scale::px(4.0f));
          std::string key_text = short_key_name(mapped->second);
          if (key_text.empty()) {
            const char *fallback = ImGui::GetKeyName(mapped->second);
            key_text = fallback ? fallback : "";
          }
          float text_top = bar_max.y + ui::scale::px(3.0f);
          if (is_white && piano) {
            text_top =
                key_min.y + key_height * kBlackKeyHeight + ui::scale::px(3.0f);
          }
          const ImVec2 text_size = ImGui::CalcTextSize(key_text.c_str());
          draw_list->AddText(
              ImVec2(key_min.x + (key_width - text_size.x) * 0.5f, text_top),
              text_color, key_text.c_str());
          key_name_bottom = text_top + text_size.y;
        }
        // The note's name at the key's foot, left out where the keys are too
        // short for it to clear the typing name.
        if (note.key == keyboard_settings.key) {
          const std::string key_text = note.name();
          const ImVec2 text_size = ImGui::CalcTextSize(key_text.c_str());
          const float top = key_max.y - text_size.y - 5.0f;
          if (top >= key_name_bottom + ui::scale::px(2.0f)) {
            draw_list->AddText(
                ImVec2(key_min.x + (key_width - text_size.x) * 0.5f, top),
                text_color, key_text.c_str());
          }
        }
      }
    }

    // One bar along the top of the keys typing plays, over the white and
    // black keys alike.
    if (typed_min < typed_max) {
      draw_list->AddRectFilled(ImVec2(typed_min, y),
                               ImVec2(typed_max, y + ui::scale::px(4.0f)),
                               white_key_pressed_col);
    }

    // Once the keys typing plays move, the keyboard scrolls to centre them
    // unless all of them are in view already.
    ImGuiWindow *keys_window = ImGui::GetCurrentWindow();
    if (context.state.typing_range_moved && typed_min < typed_max) {
      context.state.typing_range_moved = false;
      const ImRect &shown = keys_window->InnerRect;
      if (typed_min < shown.Min.x || typed_max > shown.Max.x) {
        ImGui::SetScrollFromPosX(
            (typed_min + typed_max) * 0.5f - keys_window->Pos.x, 0.5f);
        keep_drawing();
      }
    }

    std::vector<ym2612::Note> wanted;
    if (pointer_held && under_pointer) {
      wanted.push_back(*under_pointer);
    }
    const ImRect visible = keys_window->InnerClipRect;
    auto &on_keyboard = context.state.fingers_on_keyboard;
    std::erase_if(on_keyboard, [](const auto &entry) {
      const auto fingers = touch::fingers();
      return std::none_of(fingers.begin(), fingers.end(),
                          [&](const auto &f) { return f.id == entry.first; });
    });
    // A finger plays only if it came down on the keyboard itself, not on a
    // dialog or a window over it.
    const auto lands_here = [&visible](ImVec2 pos) {
      if (!visible.Contains(pos) || ImGui::GetTopMostPopupModal() != nullptr) {
        return false;
      }
      ImGuiWindow *hovered = nullptr;
      ImGuiWindow *under_moving = nullptr;
      ImGui::FindHoveredWindowEx(pos, false, &hovered, &under_moving);
      return hovered == ImGui::GetCurrentWindow();
    };
    for (const auto &finger : touch::fingers()) {
      const auto [entry, first_seen] =
          on_keyboard.try_emplace(finger.id, lands_here(finger.pos));
      if (!entry->second) {
        continue;
      }
      if (first_seen) {
        touch::claim(finger.id);
      }
      if (!visible.Contains(finger.pos)) {
        continue;
      }
      for (const auto &key : key_rects) {
        if (key.rect.Contains(finger.pos)) {
          if (std::find(wanted.begin(), wanted.end(), key.note) ==
              wanted.end()) {
            wanted.push_back(key.note);
          }
          break;
        }
      }
    }

    // Only what this keyboard turned on, and each of those let go for sure:
    // the chip's own list of sounding notes lags a frame behind, so a quick
    // slide would find a note it had just started not sounding yet.
    auto &held = context.state.held_notes;
    std::erase_if(held, [&](const ym2612::Note &note) {
      if (std::find(wanted.begin(), wanted.end(), note) != wanted.end()) {
        return false;
      }
      if (context.key_off) {
        context.key_off(note);
      }
      return true;
    });
    for (const auto &note : wanted) {
      // Not one another keyboard is already holding.
      if (std::find(held.begin(), held.end(), note) == held.end() &&
          !(context.key_is_pressed && context.key_is_pressed(note)) &&
          context.key_on) {
        context.key_on(note, 127);
        held.push_back(note);
      }
    }
  } else {
    release_held_keys(context);
  }
  ImGui::EndChild();

  ImGui::End();
};

namespace {

/// The switch's two faces at `fit`, under one ID so it stays the same item
/// as it toggles.
std::pair<const char *, const char *> scale_play_labels(BarCentreFit fit) {
  if (fit == BarCentreFit::Icon) {
    return {ICON_MEGATOY_PLAY "###scale_play",
            ICON_MEGATOY_STOP "###scale_play"};
  }
  return {ICON_MEGATOY_PLAY " Play###scale_play",
          ICON_MEGATOY_STOP " Stop###scale_play"};
}

float scale_play_switch_width(BarCentreFit fit) {
  const auto [play, stop] = scale_play_labels(fit);
  return std::max(ImGui::CalcTextSize(play, nullptr, true).x,
                  ImGui::CalcTextSize(stop, nullptr, true).x);
}

std::string scale_play_note(const MidiKeyboardContext &context) {
  return scale_title(context.input_state) + " Scale";
}

} // namespace

BarCentreSize scale_play_size(const MidiKeyboardContext &context,
                              BarCentreFit fit) {
  const float width = scale_play_switch_width(fit);
  if (fit != BarCentreFit::Full) {
    return {width, width};
  }
  return {width, width + ImGui::GetStyle().ItemSpacing.x +
                     ImGui::CalcTextSize(scale_play_note(context).c_str()).x};
}

void render_scale_play(MidiKeyboardContext &context, BarCentreFit fit,
                       float left, float right, float row_y) {
  const ImGuiStyle &style = ImGui::GetStyle();
  const auto [play, stop] = scale_play_labels(fit);
  const float width = scale_play_switch_width(fit);
  const float min_x = left + style.ItemSpacing.x;
  const float max_x = right - style.ItemSpacing.x;
  if (max_x - min_x < width) {
    return;
  }

  const bool playing = context.state.playback.playing;
  const float x = std::clamp(ImGui::GetWindowWidth() * 0.5f - width * 0.5f,
                             min_x, max_x - width);
  ImGui::SetCursorPos(ImVec2(x, row_y));
  ImGui::BeginDisabled(!playing && !can_play_scale(context));
  ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, bar_item_spacing());
  if (ImGui::Selectable(playing ? stop : play, false,
                        ImGuiSelectableFlags_None, ImVec2(width, 0.0f))) {
    if (playing) {
      stop_playback(context);
    } else {
      start_playback(context);
    }
  }
  ImGui::PopStyleVar();
  ImGui::EndDisabled();

  const std::string note = scale_play_note(context);
  if (fit == BarCentreFit::Full &&
      x + width + style.ItemSpacing.x + ImGui::CalcTextSize(note.c_str()).x <=
          max_x) {
    ImGui::SameLine();
    ImGui::TextColored(styles::color(styles::MegatoyCol::TextMuted), "%s",
                       note.c_str());
  } else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("%s", note.c_str());
  }
}

} // namespace ui
