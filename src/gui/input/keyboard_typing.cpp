#include "keyboard_typing.hpp"
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

const std::map<ImGuiKey, ym2612::Note>
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

void check_keyboard_typing(
    KeyboardTypingContext &context,
    const std::map<ImGuiKey, ym2612::Note> key_mappings) {
  auto &input = context.input_state;

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
    if (input.keyboard_typing_octave < 7) {
      input.keyboard_typing_octave++;
    }
  }

  key_pressed_last_frame[ImGuiKey_Comma] = comma_pressed_now;
  key_pressed_last_frame[ImGuiKey_Period] = period_pressed_now;

  for (auto &[imgui_key, note] : key_mappings) {
    bool key_pressed_now = ImGui::IsKeyDown(imgui_key);
    bool key_was_pressed = key_pressed_last_frame[imgui_key];

    if (key_pressed_now && !key_was_pressed) {
      if (context.key_on) {
        context.key_on(note, 127);
      }
      input.active_keyboard_notes[static_cast<int>(imgui_key)] = note;
    } else if (!key_pressed_now && key_was_pressed) {
      auto it = input.active_keyboard_notes.find(static_cast<int>(imgui_key));
      if (it != input.active_keyboard_notes.end()) {
        if (context.key_off) {
          context.key_off(it->second);
        }
        input.active_keyboard_notes.erase(it); // Remove from active notes
      }
    }
    key_pressed_last_frame[imgui_key] = key_pressed_now;
  }
}

} // namespace ui
