#include "midi_keyboard.hpp"
#include "core/types.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "keyboard_typing.hpp"
#include "util.hpp"
#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <vector>

namespace ui {
struct NoteHash {
  std::size_t operator()(const ym2612::Note &note) const {
    return std::hash<int>()(note.octave) * 12 +
           std::hash<int>()(static_cast<int>(note.key));
  }
};

void render_midi_keyboard(const char *title, AppState &app_state) {
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

  auto &input = app_state.input_state();
  auto &keyboard_settings = input.midi_keyboard_settings;
  const auto key_mappings =
      create_key_mappings(keyboard_settings.scale, keyboard_settings.key,
                          input.keyboard_typing_octave);
  if (!ImGui::GetIO().WantTextInput) {
    check_keyboard_typing(app_state, key_mappings);
  }

  auto &ui_state = app_state.ui_state();
  if (!ui_state.prefs.show_midi_keyboard) {
    return;
  }

  std::map<ym2612::Note, ImGuiKey> key_mappings_2;
  for (const auto &pair : key_mappings) {
    key_mappings_2[pair.second] = pair.first;
  }

  ImGui::SetNextWindowSize(ImVec2(400, 180), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(title, &ui_state.prefs.show_midi_keyboard)) {
    ImGui::End();
    return;
  }

  ImGui::Columns(2, "settings_columns", false);

  // Scale selector
  int current_scale = static_cast<int>(keyboard_settings.scale);
  if (ImGui::Combo("Scale", &current_scale, scale_names,
                   IM_ARRAYSIZE(scale_names))) {
    keyboard_settings.scale = static_cast<Scale>(current_scale);
    if (keyboard_settings.scale == Scale::CHROMATIC) {
      keyboard_settings.key = Key::C;
    }
  }

  ImGui::NextColumn();

  if (keyboard_settings.scale == Scale::CHROMATIC)
    ImGui::BeginDisabled();
  // Key selector
  int current_key = static_cast<int>(keyboard_settings.key);
  if (ImGui::Combo("Key", &current_key, key_names, IM_ARRAYSIZE(key_names))) {
    keyboard_settings.key = static_cast<Key>(current_key);
  }
  if (keyboard_settings.scale == Scale::CHROMATIC)
    ImGui::EndDisabled();

  ImGui::NextColumn();

  int current_key_octave = static_cast<int>(input.keyboard_typing_octave);
  std::string key_text = key_mappings.at(ImGuiKey_A).name() + "-" +
                         key_mappings.at(ImGuiKey_Semicolon).name();
  if (ImGui::SliderInt("Keyboard Typing", &current_key_octave, 0, 7,
                       key_text.c_str())) {
    input.keyboard_typing_octave = current_key_octave;
  }

  ImGui::Columns(1);
  ImGui::Spacing();

  // === Scrollable keyboard area ===
  auto keys =
      keys_from_scale_and_key(keyboard_settings.scale, keyboard_settings.key);

  // Query available region
  ImVec2 available_region = ImGui::GetContentRegionAvail();

  // Create the scrollable child window
  if (ImGui::BeginChild("KeyboardScroll", ImVec2(0, available_region.y), true,
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    std::vector<ym2612::Note> notes;
    for (int midi_note = ui::midi_note_start; midi_note <= ui::midi_note_end;
         ++midi_note) {
      ym2612::Note note = ym2612::Note::from_midi_note(midi_note);
      bool has_key =
          std::find(keys.begin(), keys.end(), note.key) != keys.end();
      if (has_key)
        notes.push_back(note);
    }
    const float key_width =
        std::max(14.0f, (available_region.x - 16) / notes.size());

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImVec2 cursor = ImGui::GetCursorScreenPos();

    auto i = static_cast<int>(midi_note_start);
    // Reserve space for the child content

    float key_height = ImGui::GetContentRegionAvail().y;
    float y = ImGui::GetCursorScreenPos().y;
    float x = cursor.x;

    for (auto note : notes) {
      bool is_key_mapped = key_mappings_2.find(note) != key_mappings_2.end();

      bool is_white = ui::is_white_key(note.key);
      bool is_pressed = app_state.key_is_pressed(note);

      ImVec2 key_min(x, y);
      ImVec2 key_max(x + key_width, y + key_height);

      ImGui::SetCursorScreenPos(key_min);
      ImGui::PushID(note.midi_note());

      ImGui::InvisibleButton("white_key", ImVec2(key_width, key_height));

      bool key_is_active = ImGui::IsItemActive();
      bool key_was_deactivated = ImGui::IsItemDeactivated();
      if (key_is_active && !is_pressed) {
        app_state.key_on(note, 127);
      }

      if (key_was_deactivated && is_pressed) {
        app_state.key_off(note);
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
        std::string key_text = key_mappings_2[note] == ImGuiKey_Semicolon
                                   ? ";"
                                   : ImGui::GetKeyName(key_mappings_2[note]);
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
