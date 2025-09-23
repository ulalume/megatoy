#include "midi_keyboard.hpp"
#include "../types.hpp"
#include "util.hpp"
#include <algorithm>
#include <cstring>
#include <imgui.h>
#include <sstream>

namespace ui {

void render_midi_keyboard(AppState &app_state) {

  ImVec4 text_col_vec = ImGui::GetStyle().Colors[ImGuiCol_Text];
  ImU32 text_col = ImGui::ColorConvertFloat4ToU32(text_col_vec);
  ImVec4 bg_col_vec = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
  bg_col_vec.w = 1.0f;
  ImU32 bg_col = ImGui::ColorConvertFloat4ToU32(bg_col_vec);

  ImVec4 black_key_col_vec =
      ImVec4(std::clamp((bg_col_vec.x + 0.1f), 0.0f, 1.0f),
             std::clamp((bg_col_vec.y + 0.1f), 0.0f, 1.0f),
             std::clamp((bg_col_vec.z + 0.1f), 0.0f, 1.0f), 1.0f);
  ImU32 black_key_col = ImGui::ColorConvertFloat4ToU32(black_key_col_vec);
  ImU32 black_key_pressed_col = bg_col;

  ImVec4 white_key_pressed_col_vec =
      ImVec4(std::clamp((text_col_vec.x - 0.1f) * 0.8f, 0.0f, 1.0f),
             std::clamp((text_col_vec.y - 0.1f) * 0.8f, 0.0f, 1.0f),
             std::clamp((text_col_vec.z - 0.1f) * 0.8f, 0.0f, 1.0f), 1.0f);
  ImU32 white_key_col = text_col;
  ImU32 white_key_pressed_col =
      ImGui::ColorConvertFloat4ToU32(white_key_pressed_col_vec);

  auto &ui_state = app_state.ui_state();
  if (!ui_state.show_midi_keyboard) {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(400, 180), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("SoftKeyboard", &ui_state.show_midi_keyboard)) {
    ImGui::End();
    return;
  }

  ImGui::Columns(2, "settings_columns", false);

  auto &input = app_state.input_state();
  auto &keyboard_settings = input.midi_keyboard_settings;

  // Scale selector
  int current_scale = static_cast<int>(keyboard_settings.scale);
  if (ImGui::Combo("Scale", &current_scale, scale_names,
                   IM_ARRAYSIZE(scale_names))) {
    keyboard_settings.scale = static_cast<Scale>(current_scale);
  }

  ImGui::NextColumn();

  // Key selector
  int current_key = static_cast<int>(keyboard_settings.key);
  if (ImGui::Combo("Key", &current_key, key_names, IM_ARRAYSIZE(key_names))) {
    keyboard_settings.key = static_cast<Key>(current_key);
  }

  ImGui::Columns(1);
  ImGui::Spacing();

  // === Scrollable keyboard area ===
  auto keys =
      keys_from_scale_and_key(keyboard_settings.scale, keyboard_settings.key);

  // Compute total keyboard width
  const float key_width = 18.0f;
  float total_keyboard_width = keys.size() * key_width;

  // Query available region
  ImVec2 available_region = ImGui::GetContentRegionAvail();

  // Create the scrollable child window
  if (ImGui::BeginChild("KeyboardScroll", ImVec2(0, available_region.y), true,
                        ImGuiWindowFlags_HorizontalScrollbar)) {

    const float key_height = ImGui::GetContentRegionAvail().y;
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImVec2 cursor = ImGui::GetCursorScreenPos();

    // Reserve space for the child content
    ImGui::Dummy(ImVec2(total_keyboard_width, key_height));

    float x = cursor.x;

    for (int midi_note = ui::midi_note_start; midi_note < ui::midi_note_end;
         ++midi_note) {
      ym2612::Note note = ym2612::Note::from_midi_note(midi_note);

      bool has_key =
          std::find(keys.begin(), keys.end(), note.key) != keys.end();
      if (!has_key)
        continue;

      bool is_white = ui::is_white_key(note.key);
      bool is_pressed = app_state.key_is_pressed(note);

      ImVec2 key_min(x, cursor.y);
      ImVec2 key_max(x + key_width, cursor.y + key_height);

      ImGui::SetCursorScreenPos(key_min);
      ImGui::PushID(midi_note);

      ImGui::InvisibleButton("white_key", ImVec2(key_width, key_height));

      bool key_is_active = ImGui::IsItemActive();
      bool key_was_deactivated = ImGui::IsItemDeactivated();
      if (key_is_active && !is_pressed) {
        app_state.key_on(note);
      }

      if (key_was_deactivated && is_pressed) {
        app_state.key_off(note);
      }

      ImGui::PopID();

      ImU32 col = is_white
                      ? (is_pressed ? white_key_pressed_col : white_key_col)
                      : (is_pressed ? black_key_pressed_col : black_key_col);
      draw_list->AddRectFilled(key_min, key_max, col);
      draw_list->AddRect(key_min, key_max, bg_col, 0, 0, 1.0f);

      if (note.key == keyboard_settings.key) {
        ImU32 text_color = is_white ? bg_col : text_col;
        std::stringstream key_name;
        key_name << note;
        std::string key_text = key_name.str();
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
