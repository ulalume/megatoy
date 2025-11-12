#include "preferences.hpp"
#include "gui/input/key_name_utils.hpp"
#include "gui/input/typing_keyboard_layout.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/styles/theme.hpp"
#include <algorithm>
#include <array>
#include <imgui.h>
#include <map>
#include <string>

namespace ui {

namespace {

struct CustomLayoutEditorState {
  enum class OctaveCaptureTarget { None, Down, Up };
  int slot_capture_index = -1;
  OctaveCaptureTarget octave_capture_target = OctaveCaptureTarget::None;
};

CustomLayoutEditorState &custom_layout_editor_state() {
  static CustomLayoutEditorState state;
  return state;
}

void normalize_custom_layout(UIPreferences &prefs) {
  auto &keys = prefs.custom_typing_layout_keys;
  if (keys.empty()) {
    keys.resize(ui::typing_layout_min_custom_slots,
                static_cast<int>(ImGuiKey_None));
  }
  if (keys.size() < ui::typing_layout_min_custom_slots) {
    keys.resize(ui::typing_layout_min_custom_slots,
                static_cast<int>(ImGuiKey_None));
  } else if (keys.size() > ui::typing_layout_max_custom_slots) {
    keys.resize(ui::typing_layout_max_custom_slots);
  }
  const auto defaults = default_octave_keys(TypingKeyboardLayout::Custom);
  prefs.custom_typing_octave_down_key = static_cast<int>(
      key_from_preference(prefs.custom_typing_octave_down_key, defaults.first));
  prefs.custom_typing_octave_up_key = static_cast<int>(
      key_from_preference(prefs.custom_typing_octave_up_key, defaults.second));
}

void clamp_capture_state(CustomLayoutEditorState &state,
                         const UIPreferences &prefs) {
  if (state.slot_capture_index >=
      static_cast<int>(prefs.custom_typing_layout_keys.size())) {
    state.slot_capture_index = -1;
  }
}

void poll_custom_layout_capture(CustomLayoutEditorState &state,
                                UIPreferences &prefs) {
  if (state.slot_capture_index < 0 &&
      state.octave_capture_target ==
          CustomLayoutEditorState::OctaveCaptureTarget::None) {
    return;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    state.slot_capture_index = -1;
    state.octave_capture_target =
        CustomLayoutEditorState::OctaveCaptureTarget::None;
    return;
  }
  for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
    ImGuiKey imgui_key = static_cast<ImGuiKey>(key);
    if (imgui_key == ImGuiKey_None) {
      continue;
    }
    if (ImGui::IsKeyPressed(imgui_key)) {
      if (state.slot_capture_index >= 0 &&
          static_cast<std::size_t>(state.slot_capture_index) <
              prefs.custom_typing_layout_keys.size()) {
        prefs.custom_typing_layout_keys[static_cast<std::size_t>(
            state.slot_capture_index)] = key;
        state.slot_capture_index = -1;
      } else if (state.octave_capture_target ==
                 CustomLayoutEditorState::OctaveCaptureTarget::Down) {
        prefs.custom_typing_octave_down_key = key;
        state.octave_capture_target =
            CustomLayoutEditorState::OctaveCaptureTarget::None;
      } else if (state.octave_capture_target ==
                 CustomLayoutEditorState::OctaveCaptureTarget::Up) {
        prefs.custom_typing_octave_up_key = key;
        state.octave_capture_target =
            CustomLayoutEditorState::OctaveCaptureTarget::None;
      }
      break;
    }
  }
}

std::string slot_note_label(std::size_t slot_index) {
  static constexpr std::array<const char *, 12> note_names{
      "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  std::string label = note_names[slot_index % note_names.size()];
  const std::size_t octave = slot_index / note_names.size();
  if (octave > 0) {
    label += " (+" + std::to_string(octave) + ")";
  }
  return label;
}

std::string button_label_for_key(ImGuiKey key) {
  if (key == ImGuiKey_None) {
    return "Unassigned";
  }
  std::string label = short_key_name(key);
  if (label.empty() || label == "??") {
    const char *fallback = ImGui::GetKeyName(key);
    label = fallback ? fallback : "??";
  }
  return label;
}

void render_octave_control_button(
    const char *label, const char *id_suffix, int &stored_key,
    CustomLayoutEditorState &state,
    CustomLayoutEditorState::OctaveCaptureTarget target) {
  ImGuiKey current_key = key_from_preference(stored_key, ImGuiKey_None);
  std::string button_text = button_label_for_key(current_key);
  std::string button_id = button_text + "##" + id_suffix;
  ImGui::TextUnformatted(label);
  ImGui::SameLine();
  if (ImGui::Button(button_id.c_str())) {
    state.slot_capture_index = -1;
    state.octave_capture_target = target;
  }
  if (state.octave_capture_target == target) {
    ImGui::SameLine();
    ImGui::TextColored(styles::color(styles::MegatoyCol::StatusWarning),
                       "Press a key...");
    ImGui::SameLine();
    std::string cancel_id = std::string("Cancel##") + id_suffix;
    if (ImGui::SmallButton(cancel_id.c_str())) {
      state.octave_capture_target =
          CustomLayoutEditorState::OctaveCaptureTarget::None;
    }
  }
  ImGui::SameLine();
  std::string clear_id = std::string("Clear##") + id_suffix;
  if (ImGui::SmallButton(clear_id.c_str())) {
    stored_key = static_cast<int>(ImGuiKey_None);
  }
}

void render_custom_layout_editor(UIPreferences &prefs) {
  auto &state = custom_layout_editor_state();
  normalize_custom_layout(prefs);
  clamp_capture_state(state, prefs);
  poll_custom_layout_capture(state, prefs);

  ImGui::TextWrapped(
      "Click a slot, then press a keyboard key to assign it to that note.");
  ImGui::TextWrapped("Use the buttons below to add or remove slots.");

  const bool can_add_slot = prefs.custom_typing_layout_keys.size() <
                            ui::typing_layout_max_custom_slots;
  if (!can_add_slot)
    ImGui::BeginDisabled();
  if (ImGui::Button("+ Add Key")) {
    prefs.custom_typing_layout_keys.push_back(static_cast<int>(ImGuiKey_None));
  }
  if (!can_add_slot)
    ImGui::EndDisabled();

  ImGui::SameLine();

  const bool can_remove_slot = prefs.custom_typing_layout_keys.size() >
                               ui::typing_layout_min_custom_slots;
  if (!can_remove_slot)
    ImGui::BeginDisabled();
  if (ImGui::Button("- Remove Key")) {
    prefs.custom_typing_layout_keys.pop_back();
    if (state.slot_capture_index >=
        static_cast<int>(prefs.custom_typing_layout_keys.size())) {
      state.slot_capture_index = -1;
    }
  }
  if (!can_remove_slot)
    ImGui::EndDisabled();

  std::map<ImGuiKey, int> key_usage;
  bool has_unassigned = false;
  for (const auto key_value : prefs.custom_typing_layout_keys) {
    ImGuiKey key = key_from_preference(key_value, ImGuiKey_None);
    if (key == ImGuiKey_None) {
      has_unassigned = true;
      continue;
    }
    key_usage[key]++;
  }

  bool has_duplicates = false;
  if (ImGui::BeginTable("CustomTypingLayout", 3,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                            ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupColumn("Note", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (std::size_t i = 0; i < prefs.custom_typing_layout_keys.size(); ++i) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%s", slot_note_label(i).c_str());

      ImGui::TableSetColumnIndex(1);
      ImGui::PushID(static_cast<int>(i));
      ImGuiKey key = key_from_preference(prefs.custom_typing_layout_keys[i],
                                         ImGuiKey_None);
      std::string button_text = button_label_for_key(key);
      if (ImGui::Button(button_text.c_str())) {
        state.slot_capture_index = static_cast<int>(i);
        state.octave_capture_target =
            CustomLayoutEditorState::OctaveCaptureTarget::None;
      }
      if (state.slot_capture_index == static_cast<int>(i)) {
        ImGui::SameLine();
        ImGui::TextColored(styles::color(styles::MegatoyCol::StatusWarning),
                           "Press a key...");
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel")) {
          state.slot_capture_index = -1;
        }
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Clear")) {
        prefs.custom_typing_layout_keys[i] = static_cast<int>(ImGuiKey_None);
      }
      ImGui::PopID();

      ImGui::TableSetColumnIndex(2);
      std::string status;
      if (key == ImGuiKey_None) {
        status = "Unassigned";
      } else if (key_usage[key] > 1) {
        status = "Duplicate";
        has_duplicates = true;
      }
      if (!status.empty()) {
        ImGui::TextColored(styles::color(styles::MegatoyCol::StatusWarning),
                           "%s", status.c_str());
      } else {
        ImGui::TextColored(styles::color(styles::MegatoyCol::StatusSuccess),
                           "OK");
      }
    }
    ImGui::EndTable();
  }

  if (has_duplicates) {
    ImGui::TextColored(styles::color(styles::MegatoyCol::StatusWarning),
                       "Duplicate keys will trigger the same note twice.");
  }
  if (has_unassigned) {
    ImGui::TextColored(styles::color(styles::MegatoyCol::StatusWarning),
                       "Empty slots won't play notes.");
  }

  render_octave_control_button(
      "Octave Down", "octave_down", prefs.custom_typing_octave_down_key, state,
      CustomLayoutEditorState::OctaveCaptureTarget::Down);
  render_octave_control_button(
      "Octave Up", "octave_up", prefs.custom_typing_octave_up_key, state,
      CustomLayoutEditorState::OctaveCaptureTarget::Up);

  if (ImGui::Button("Reset to default QWERTY Layout")) {
    copy_builtin_to_preferences(prefs.custom_typing_layout_keys,
                                qwerty_typing_layout_keys);
    normalize_custom_layout(prefs);
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset to default AZERTY Layout")) {
    copy_builtin_to_preferences(prefs.custom_typing_layout_keys,
                                azerty_typing_layout_keys);
    normalize_custom_layout(prefs);
  }
}

} // namespace

void render_preferences_window(const char *title, PreferencesContext &context) {
  auto &prefs = context.preferences;
  auto &ui_prefs = context.ui_prefs;
  normalize_custom_layout(ui_prefs);
  if (!ui_prefs.show_preferences) {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(480, 260), ImGuiCond_FirstUseEver);

  if (ImGui::Begin(title, &ui_prefs.show_preferences)) {

#if !defined(MEGATOY_PLATFORM_WEB)
    // Show the current directory
    ImGui::SeparatorText("Data Directory");
    ImGui::TextWrapped("%s", context.paths.data_root.c_str());
    ImGui::Spacing();

    if (ImGui::Button("Select Directory...")) {
      context.open_directory_dialog = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset to Default")) {
      prefs.reset_data_directory();
      if (context.sync_patch_directories) {
        context.sync_patch_directories();
      }
    }
    if (prefs.is_initialized()) {
      ImGui::TextColored(styles::color(styles::MegatoyCol::StatusSuccess),
                         "Directories initialized");
    } else {
      ImGui::TextColored(styles::color(styles::MegatoyCol::StatusError),
                         "Directory initialization failed");
      if (ImGui::Button("Retry Directory Creation")) {
        if (prefs.ensure_directories_exist()) {
          if (context.sync_patch_directories) {
            context.sync_patch_directories();
          }
        }
      }
    }
#endif

    ImGui::SeparatorText("Theme");

    const auto &themes = ui::styles::available_themes();
    int current_theme_index = 0;
    auto current_theme = prefs.theme();
    for (int i = 0; i < static_cast<int>(themes.size()); ++i) {
      if (themes[i].id == current_theme) {
        current_theme_index = i;
        break;
      }
    }

    const char *theme_preview =
        themes.empty() ? "" : themes[current_theme_index].display_name;
    if (ImGui::BeginCombo("##UI Theme", theme_preview)) {
      for (int i = 0; i < static_cast<int>(themes.size()); ++i) {
        const bool is_selected = (i == current_theme_index);
        if (ImGui::Selectable(themes[i].display_name, is_selected)) {
          current_theme_index = i;
          auto selected_id = themes[i].id;
          prefs.set_theme(selected_id);
          if (context.apply_theme) {
            context.apply_theme(selected_id);
          }
        }
        if (is_selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    ImGui::SeparatorText("MIDI Input");

    ImGui::Checkbox("Use MIDI velocity", &ui_prefs.use_velocity);
    if (!ui_prefs.use_velocity) {
      ImGui::TextWrapped("Notes play at full velocity.");
    }

    ImGui::Checkbox("Steal oldest note when all 6 channels are busy",
                    &ui_prefs.steal_oldest_note_when_full);

    if (!context.midi_status_message.empty()) {
      ImGui::TextWrapped("%s", context.midi_status_message.c_str());
    }
    if (context.show_web_midi_button) {
      if (context.web_midi_button_disabled) {
        ImGui::BeginDisabled();
      }
      if (ImGui::Button("Enable WebMIDI")) {
        if (context.request_web_midi) {
          context.request_web_midi();
        }
      }
      if (context.web_midi_button_disabled) {
        ImGui::EndDisabled();
      }
    }

    if (context.connected_midi_devices.empty()) {
      ImGui::TextUnformatted("No MIDI devices detected.");
    } else {
      ImGui::Text("Connected devices (%zu)",
                  context.connected_midi_devices.size());
      ImGui::Indent();
      for (const auto &name : context.connected_midi_devices) {
        ImGui::BulletText("%s", name.c_str());
      }
      ImGui::Unindent();
    }

    ImGui::SeparatorText("Typing Keyboard");
    const int layout_count =
        static_cast<int>(ui::typing_keyboard_layout_names.size());
    ui_prefs.midi_keyboard_layout =
        std::clamp(ui_prefs.midi_keyboard_layout, 0, layout_count - 1);
    int current_layout = ui_prefs.midi_keyboard_layout;
    if (ImGui::Combo("Layout", &current_layout,
                     ui::typing_keyboard_layout_names.data(), layout_count)) {
      ui_prefs.midi_keyboard_layout = current_layout;
    }
    const auto selected_layout =
        clamp_layout_pref(ui_prefs.midi_keyboard_layout);
    if (selected_layout == TypingKeyboardLayout::Custom) {
      ImGui::Spacing();
      render_custom_layout_editor(ui_prefs);
    }
  }

  ImGui::End();

  if (context.open_directory_dialog) {
    context.open_directory_dialog = false;
    if (prefs.select_data_directory()) {
      if (context.sync_patch_directories) {
        context.sync_patch_directories();
      }
    }
  }
}
} // namespace ui
