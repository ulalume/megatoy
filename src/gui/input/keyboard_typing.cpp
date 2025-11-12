#include "keyboard_typing.hpp"
#include "typing_keyboard_layout.hpp"
#include <array>
#include <cstring>
#include <imgui.h>
#include <map>
#include <vector>

namespace ui {

namespace {

constexpr std::array<int, 7> white_key_offsets{0, 2, 4, 5, 7, 9, 11};

std::vector<size_t> non_chromatic_indices(size_t max_notes) {
  std::vector<size_t> indices;
  for (size_t octave = 0;; ++octave) {
    bool added = false;
    for (int offset : white_key_offsets) {
      const size_t index = octave * 12 + static_cast<size_t>(offset);
      if (index >= max_notes) {
        return indices;
      }
      indices.push_back(index);
      added = true;
    }
    if (!added) {
      break;
    }
  }
  return indices;
}

std::vector<ImGuiKey> keys_for_scale(Scale scale,
                                     const TypingLayout &base_layout) {
  if (scale == Scale::CHROMATIC) {
    return base_layout;
  }

  std::vector<ImGuiKey> filtered;
  if (base_layout.empty()) {
    return filtered;
  }

  const size_t playable_keys = base_layout.back() == ImGuiKey_None
                                   ? base_layout.size() - 1
                                   : base_layout.size();
  const auto indices = non_chromatic_indices(playable_keys);
  filtered.reserve(indices.size() + 1);
  for (const auto idx : indices) {
    if (idx >= base_layout.size()) {
      break;
    }
    auto key = base_layout[idx];
    if (key == ImGuiKey_None) {
      break;
    }
    filtered.push_back(key);
  }
  filtered.push_back(ImGuiKey_None);
  return filtered;
}

} // namespace

const std::map<ImGuiKey, ym2612::Note>
create_key_mappings(Scale scale, Key key, uint8_t selected_octave,
                    TypingKeyboardLayout layout,
                    const TypingLayout &custom_layout) {
  const auto base_layout = layout == TypingKeyboardLayout::Custom
                               ? custom_layout
                               : builtin_typing_layout(layout);
  const auto keyboard_keys = keys_for_scale(scale, base_layout);

  auto keys = keys_from_scale_and_key(scale, key);
  std::map<ImGuiKey, ym2612::Note> key_mappings;
  auto i = 0;
  auto old_key = 1000;
  while (true) {
    if (i >= static_cast<int>(keyboard_keys.size())) {
      break;
    }
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

  const ImGuiKey octave_down = context.octave_down_key;
  const ImGuiKey octave_up = context.octave_up_key;

  // Handle octave changes with configured keys
  bool down_pressed_now =
      octave_down != ImGuiKey_None ? ImGui::IsKeyDown(octave_down) : false;
  bool down_was_pressed = octave_down != ImGuiKey_None
                              ? key_pressed_last_frame[octave_down]
                              : false;
  bool up_pressed_now =
      octave_up != ImGuiKey_None ? ImGui::IsKeyDown(octave_up) : false;
  bool up_was_pressed =
      octave_up != ImGuiKey_None ? key_pressed_last_frame[octave_up] : false;

  if (down_pressed_now && !down_was_pressed) {
    if (input.keyboard_typing_octave > 0) {
      input.keyboard_typing_octave--;
    }
  }

  if (up_pressed_now && !up_was_pressed) {
    if (input.keyboard_typing_octave < 7) {
      input.keyboard_typing_octave++;
    }
  }

  if (octave_down != ImGuiKey_None) {
    key_pressed_last_frame[octave_down] = down_pressed_now;
  }
  if (octave_up != ImGuiKey_None) {
    key_pressed_last_frame[octave_up] = up_pressed_now;
  }

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
