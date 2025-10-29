#include "midi_keyboard.hpp"

#include "core/types.hpp"
#include "gui/input/keyboard_typing.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "util.hpp"
#include <algorithm>
#include <chord_detector.h>
#include <imgui.h>
#include <string>
#include <vector>

namespace ui {
namespace {

void refresh_state(MidiKeyboardState &state, const InputState &input) {
  const auto &settings = input.midi_keyboard_settings;
  if (state.initialized && state.cached_scale == settings.scale &&
      state.cached_key == settings.key &&
      state.cached_octave == input.keyboard_typing_octave) {
    return;
  }

  state.cached_scale = settings.scale;
  state.cached_key = settings.key;
  state.cached_octave = input.keyboard_typing_octave;

  state.key_mappings = create_key_mappings(settings.scale, settings.key,
                                           input.keyboard_typing_octave);

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

  const auto from = state.key_mappings.find(ImGuiKey_A);
  const auto to = state.key_mappings.find(ImGuiKey_Semicolon);
  if (from != state.key_mappings.end() && to != state.key_mappings.end()) {
    state.typing_range_label = from->second.name() + "-" + to->second.name();
  } else {
    state.typing_range_label.clear();
  }

  state.initialized = true;
}

const char *typing_label(const MidiKeyboardState &state) {
  return state.typing_range_label.empty() ? "Keys"
                                          : state.typing_range_label.c_str();
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

  refresh_state(context.state, input);
  const auto &key_mappings = context.state.key_mappings;

  if (!ImGui::GetIO().WantTextInput && !key_mappings.empty()) {
    KeyboardTypingContext typing_context{
        context.input_state,
        [&context](ym2612::Note note, uint8_t velocity) {
          context.key_on(note, velocity);
        },
        [&context](ym2612::Note note) { context.key_off(note); }};
    check_keyboard_typing(typing_context, key_mappings);
  }
  context.ui_prefs.midi_keyboard_typing_octave =
      static_cast<int>(input.keyboard_typing_octave);

  auto &ui_prefs = context.ui_prefs;
  if (!ui_prefs.show_midi_keyboard) {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(400, 180), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(title, &ui_prefs.show_midi_keyboard)) {
    ImGui::End();
    return;
  }

  // Scale selector
  int current_scale = static_cast<int>(keyboard_settings.scale);
  ImGui::SetNextItemWidth(150);
  if (ImGui::Combo("Scale", &current_scale, scale_names,
                   IM_ARRAYSIZE(scale_names))) {
    keyboard_settings.scale = static_cast<Scale>(current_scale);
    context.ui_prefs.midi_keyboard_scale = current_scale;
    if (keyboard_settings.scale == Scale::CHROMATIC) {
      keyboard_settings.key = Key::C;
      context.ui_prefs.midi_keyboard_key = static_cast<int>(Key::C);
    }
    refresh_state(context.state, input);
  }

  ImGui::SameLine(0, 16);

  if (keyboard_settings.scale == Scale::CHROMATIC)
    ImGui::BeginDisabled();
  // Key selector
  int current_key = static_cast<int>(keyboard_settings.key);
  ImGui::SetNextItemWidth(40);
  if (ImGui::Combo("Key", &current_key, key_names, IM_ARRAYSIZE(key_names))) {
    keyboard_settings.key = static_cast<Key>(current_key);
    context.ui_prefs.midi_keyboard_key = current_key;
    refresh_state(context.state, input);
  }
  if (keyboard_settings.scale == Scale::CHROMATIC)
    ImGui::EndDisabled();

  ImGui::SameLine(0, 32);

  int current_key_octave = static_cast<int>(input.keyboard_typing_octave);
  ImGui::SetNextItemWidth(100);
  if (ImGui::SliderInt("Typing", &current_key_octave, 0, 7,
                       typing_label(context.state))) {
    input.keyboard_typing_octave = current_key_octave;
    context.ui_prefs.midi_keyboard_typing_octave = current_key_octave;
    refresh_state(context.state, input);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("'<' key: down\n'>' key: up");
  }

  ImGui::SameLine(0, 16);

  std::vector<ym2612::Note> active_notes = context.active_notes
                                               ? context.active_notes()
                                               : std::vector<ym2612::Note>{};
  std::string chord_name;
  if (active_notes.size() >= 2) {
    std::vector<int> midi_notes;
    midi_notes.reserve(active_notes.size());
    for (auto &note : active_notes) {
      midi_notes.push_back(static_cast<int>(note.midi_note()));
    }
    chord_name = get_chord_name(midi_notes, false, true);
  }
  ImGui::Text("%s", chord_name.c_str());

  // Scrollable keyboard area
  const ImVec2 available_region = ImGui::GetContentRegionAvail();

  if (ImGui::BeginChild("KeyboardScroll", ImVec2(0, available_region.y), true,
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    const auto &notes = context.state.display_notes;
    const float key_width =
        notes.empty()
            ? 14.0f
            : std::max(14.0f, (available_region.x - 16) / notes.size());

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();

    float key_height = ImGui::GetContentRegionAvail().y;
    float y = ImGui::GetCursorScreenPos().y;
    float x = cursor.x;

    for (auto note : notes) {
      bool is_key_mapped = context.state.reverse_mappings.find(note) !=
                           context.state.reverse_mappings.end();

      bool is_white = ui::is_white_key(note.key);
      bool is_pressed =
          context.key_is_pressed ? context.key_is_pressed(note) : false;

      ImVec2 key_min(x, y);
      ImVec2 key_max(x + key_width, y + key_height);

      ImGui::SetCursorScreenPos(key_min);
      ImGui::PushID(note.midi_note());

      ImGui::InvisibleButton("white_key", ImVec2(key_width, key_height));

      bool key_is_active = ImGui::IsItemActive();
      bool key_was_deactivated = ImGui::IsItemDeactivated();
      if (key_is_active && !is_pressed) {
        if (context.key_on) {
          context.key_on(note, 127);
        }
      }

      if (key_was_deactivated && is_pressed) {
        if (context.key_off) {
          context.key_off(note);
        }
      }

      ImGui::PopID();

      ImU32 col = is_white
                      ? (is_pressed ? white_key_pressed_col : white_key_col)
                      : (is_pressed ? black_key_pressed_col : black_key_col);
      draw_list->AddRectFilled(key_min, key_max, col);
      draw_list->AddRect(key_min, key_max, key_border_col, 0, 0, 1.0f);

      if (is_key_mapped) {
        ImVec2 key_max_mapped(x + key_width, y + 2);
        draw_list->AddRectFilled(key_min, key_max_mapped,
                                 white_key_pressed_col);
        ImU32 text_color = is_white ? white_key_text_col : black_key_text_col;
        auto it = context.state.reverse_mappings.find(note);
        ImGuiKey mapped_key = (it != context.state.reverse_mappings.end())
                                  ? it->second
                                  : ImGuiKey_None;
        std::string key_text = mapped_key == ImGuiKey_Semicolon
                                   ? ";"
                                   : ImGui::GetKeyName(mapped_key);
        ImVec2 text_size = ImGui::CalcTextSize(key_text.c_str());
        ImVec2 text_pos(key_min.x + (key_width - text_size.x) * 0.5f,
                        key_max_mapped.y);
        draw_list->AddText(text_pos, text_color, key_text.c_str());
      }
      if (note.key == keyboard_settings.key) {
        ImU32 text_color = is_white ? white_key_text_col : black_key_text_col;
        std::string key_text = note.name();
        ImVec2 text_size = ImGui::CalcTextSize(key_text.c_str());
        ImVec2 text_pos(key_min.x + (key_width - text_size.x) * 0.5f,
                        key_max.y - text_size.y - 5.0f);
        draw_list->AddText(text_pos, text_color, key_text.c_str());
      }
      x += key_width;
    }
  }
  ImGui::EndChild();

  ImGui::End();
};

}; // namespace ui
