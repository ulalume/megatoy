#include "keyboard_typing.hpp"
#include <cstring>
#include <imgui.h>
#include <map>

namespace ui {

// Static variables to track key states
static std::map<ImGuiKey, bool> key_pressed_last_frame;

// Function to render the main audio control panel
void render_keyboard_typing(AppState &app_state) {
  // Create main window
  auto &ui_state = app_state.ui_state();
  if (!ui_state.prefs.show_audio_controls) {
    return;
  }

  ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin("Keyboard Typing", &ui_state.prefs.show_audio_controls)) {
    ImGui::End();
    return;
  }

  const char *key_octave_names[] = {"C1", "C2", "C3", "C4", "C5", "C6", "C7"};
  auto &input = app_state.input_state();

  int current_key_octave = static_cast<int>(input.keyboard_typing_octave);
  if (ImGui::Combo("##Keyboard layout", &current_key_octave, key_octave_names,
                   IM_ARRAYSIZE(key_octave_names))) {
    input.keyboard_typing_octave = current_key_octave;
  };

  const uint8_t selected_octave =
      static_cast<uint8_t>(input.keyboard_typing_octave) + 1;
  const std::map<ImGuiKey, ym2612::Note> key_mappings = {
      {ImGuiKey_A, {selected_octave, Key::C}},
      {ImGuiKey_W, {selected_octave, Key::C_SHARP}},
      {ImGuiKey_S, {selected_octave, Key::D}},
      {ImGuiKey_E, {selected_octave, Key::D_SHARP}},
      {ImGuiKey_D, {selected_octave, Key::E}},
      {ImGuiKey_F, {selected_octave, Key::F}},
      {ImGuiKey_T, {selected_octave, Key::F_SHARP}},
      {ImGuiKey_G, {selected_octave, Key::G}},
      {ImGuiKey_Y, {selected_octave, Key::G_SHARP}},
      {ImGuiKey_H, {selected_octave, Key::A}},
      {ImGuiKey_U, {selected_octave, Key::A_SHARP}},
      {ImGuiKey_J, {selected_octave, Key::B}},
      {ImGuiKey_K, {static_cast<uint8_t>(selected_octave + 1), Key::C}},
      {ImGuiKey_O, {static_cast<uint8_t>(selected_octave + 1), Key::C_SHARP}},
      {ImGuiKey_L, {static_cast<uint8_t>(selected_octave + 1), Key::D}},
      {ImGuiKey_P, {static_cast<uint8_t>(selected_octave + 1), Key::D_SHARP}},
      {ImGuiKey_Semicolon, {static_cast<uint8_t>(selected_octave + 1), Key::E}},
  };

  ImGui::Text("Black keys:  W E   T Y U   O P");
  ImGui::Text("White keys: A S D F G H J K L ;");
  ImGui::Text("Octave:      , (down)    . (up)");
  ImGui::Spacing();

  if (!input.text_input_focused) {
    // Handle octave changes with comma and period keys
    bool comma_pressed_now = ImGui::IsKeyDown(ImGuiKey_Comma);
    bool comma_was_pressed = key_pressed_last_frame[ImGuiKey_Comma];
    bool period_pressed_now = ImGui::IsKeyDown(ImGuiKey_Period);
    bool period_was_pressed = key_pressed_last_frame[ImGuiKey_Period];

    // Comma key: decrease octave
    if (comma_pressed_now && !comma_was_pressed) {
      if (input.keyboard_typing_octave > 0) {
        input.keyboard_typing_octave--;
      }
    }

    // Period key: increase octave
    if (period_pressed_now && !period_was_pressed) {
      if (input.keyboard_typing_octave <
          6) { // 6 corresponds to C7 (last octave)
        input.keyboard_typing_octave++;
      }
    }

    key_pressed_last_frame[ImGuiKey_Comma] = comma_pressed_now;
    key_pressed_last_frame[ImGuiKey_Period] = period_pressed_now;

    for (auto &[imgui_key, note] : key_mappings) {
      bool key_pressed_now = ImGui::IsKeyDown(imgui_key);
      bool key_was_pressed = key_pressed_last_frame[imgui_key];

      if (key_pressed_now && !key_was_pressed) {
        app_state.key_on(note, 127);
        input.active_keyboard_notes[static_cast<int>(imgui_key)] = note;
      } else if (!key_pressed_now && key_was_pressed) {
        auto it = input.active_keyboard_notes.find(static_cast<int>(imgui_key));
        if (it != input.active_keyboard_notes.end()) {
          app_state.key_off(it->second);         // Use the recorded note
          input.active_keyboard_notes.erase(it); // Remove from active notes
        }
      }
      key_pressed_last_frame[imgui_key] = key_pressed_now;
    }
  }

  ImGui::Spacing();
  ImGui::Separator();

  ImGui::Text("Active channels:");
  int active_count = 0;
  const auto &channels = app_state.active_channels();
  for (int i = 0; i < 6; i++) {
    if (channels[i]) {
      active_count++;
      ImGui::SameLine();
      ImGui::Text("CH%d", i + 1);
    }
  }
  if (active_count == 0) {
    ImGui::SameLine();
    ImGui::Text("None");
  }

  ImGui::End();
}

} // namespace ui
