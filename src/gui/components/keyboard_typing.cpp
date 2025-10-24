#include "keyboard_typing.hpp"
#include "app_state.hpp"
#include <cstring>
#include <imgui.h>
#include <map>

namespace ui {

static constexpr ImGuiKey chromatic_keyboard_keys[] = {
    ImGuiKey_A, ImGuiKey_W,         ImGuiKey_S,    ImGuiKey_E, ImGuiKey_D,
    ImGuiKey_F, ImGuiKey_T,         ImGuiKey_G,    ImGuiKey_Y, ImGuiKey_H,
    ImGuiKey_U, ImGuiKey_J,         ImGuiKey_K,    ImGuiKey_O, ImGuiKey_L,
    ImGuiKey_P, ImGuiKey_Semicolon, ImGuiKey_None,
};
static constexpr ImGuiKey none_chromatic_keyboard_keys[] = {
    ImGuiKey_A, ImGuiKey_S,         ImGuiKey_D,   ImGuiKey_F,
    ImGuiKey_G, ImGuiKey_H,         ImGuiKey_J,   ImGuiKey_K,
    ImGuiKey_L, ImGuiKey_Semicolon, ImGuiKey_None};

static const std::map<ImGuiKey, ym2612::Note>
create_key_mappings(Scale scale, Key key, uint8_t selected_octave) {
  const ImGuiKey *keyboard_keys = scale == Scale::CHROMATIC
                                      ? chromatic_keyboard_keys
                                      : none_chromatic_keyboard_keys;

  auto keys = keys_from_scale_and_key(scale, key);
  std::map<ImGuiKey, ym2612::Note> key_mappings;
  auto i = 0;
  auto old_key = 1000;
  while (true) {
    auto keyboard_key = keyboard_keys[i];
    if (keyboard_key == ImGuiKey_None)
      break;
    auto key = keys[i % keys.size()];
    if (old_key > static_cast<int>(key) && i != 0) {
      selected_octave++;
    }
    key_mappings.insert({keyboard_key, {selected_octave, key}});
    i++;
    old_key = static_cast<int>(key);
  }
  return key_mappings;
}

// Static variables to track key states
static std::map<ImGuiKey, bool> key_pressed_last_frame;

void check_keyboard_typing(AppState &app_state) {
  auto &input = app_state.input_state();
  const std::map<ImGuiKey, ym2612::Note> key_mappings = create_key_mappings(
      input.midi_keyboard_settings.scale, input.midi_keyboard_settings.key,
      input.keyboard_typing_octave);

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
    if (input.keyboard_typing_octave < 6) { // 6 corresponds to C7 (last octave)
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

// Function to render the main audio control panel
void render_keyboard_typing(const char *title, AppState &app_state) {
  // Create main window
  auto &ui_state = app_state.ui_state();
  if (!ui_state.prefs.show_audio_controls) {
    return;
  }

  ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin(title, &ui_state.prefs.show_audio_controls)) {
    ImGui::End();
    return;
  }

  const char *key_octave_names[] = {"0", "1", "2", "3", "4", "5", "6", "7"};
  auto &input = app_state.input_state();

  int current_key_octave = static_cast<int>(input.keyboard_typing_octave);
  if (ImGui::Combo("Octave", &current_key_octave, key_octave_names,
                   IM_ARRAYSIZE(key_octave_names))) {
    input.keyboard_typing_octave = current_key_octave;
  };

  if (input.midi_keyboard_settings.scale == Scale::CHROMATIC) {
    ImGui::Text("Black keys:  W E   T Y U   O P");
    ImGui::Text("White keys: A S D F G H J K L ;");
  } else {
    ImGui::Text("Keys: A S D F G H J K L ;");
  }
  ImGui::Text("Octave:      , (down)    . (up)");
  ImGui::Spacing();

  if (!ImGui::GetIO().WantTextInput) {
    check_keyboard_typing(app_state);
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
