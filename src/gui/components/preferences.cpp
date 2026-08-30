#include "preferences.hpp"
#include "gui/envelope/envelope_curve.hpp"
#include "gui/input/key_name_utils.hpp"
#include "gui/input/typing_keyboard_layout.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/styles/theme.hpp"
#include "gui/ui_scale.hpp"
#include "platform/platform_config.hpp"
#include "ym2612/note.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

std::string scale_label(float factor) {
  return std::to_string(static_cast<int>(std::lround(factor * 100.0f))) + "%";
}

void render_ui_scale_combo(PreferencesContext &context) {
  auto &ui_prefs = context.ui_prefs;

  std::vector<std::string> labels;
  labels.push_back(
      "Auto (" +
      scale_label(ui::scale::resolve(ui::scale::kAuto, context.display_scale)) +
      ")");
  for (const float factor : ui::scale::kChoices) {
    labels.push_back(scale_label(factor));
  }

  std::vector<const char *> label_pointers;
  label_pointers.reserve(labels.size());
  for (const auto &label : labels) {
    label_pointers.push_back(label.c_str());
  }

  int index = 0;
  for (int i = 0; i < static_cast<int>(std::size(ui::scale::kChoices)); ++i) {
    if (ui_prefs.ui_scale == ui::scale::kChoices[i]) {
      index = i + 1;
      break;
    }
  }

  if (ImGui::Combo("Interface scale", &index, label_pointers.data(),
                   static_cast<int>(label_pointers.size()))) {
    ui_prefs.ui_scale =
        index == 0 ? ui::scale::kAuto : ui::scale::kChoices[index - 1];
    if (context.apply_ui_scale) {
      context.apply_ui_scale(ui_prefs.ui_scale);
    }
  }
}

void render_general_tab(PreferencesContext &context) {
  auto &prefs = context.preferences;
  auto &ui_prefs = context.ui_prefs;

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
  if (ImGui::BeginCombo("Theme", theme_preview)) {
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

  render_ui_scale_combo(context);

  ImGui::Spacing();

  static constexpr const char *multi_edit_modes[] = {
      "Relative (keep the distance between operators)",
      "Absolute (set every selected operator to the same value)"};
  int multi_edit_mode = ui_prefs.multi_operator_edit_absolute ? 1 : 0;
  if (ImGui::Combo("Multi-operator edit", &multi_edit_mode, multi_edit_modes,
                   static_cast<int>(std::size(multi_edit_modes)))) {
    ui_prefs.multi_operator_edit_absolute = multi_edit_mode == 1;
  }
  ImGui::TextWrapped("Applies while several operators are selected.");
}

// Named for what the list is rather than what it holds: the built-in presets
// switch below it is a patch source too, and it has nowhere to sit under a
// heading that only admits folders.
void render_patches_tab(PreferencesContext &context) {
  ImGui::SeparatorText("Sources");
  ImGui::TextWrapped("megatoy reads patches from the folders you add here.");
  ImGui::Spacing();

  const auto &folders = context.preferences.workspace().folders();
  if (folders.empty()) {
    ImGui::TextColored(styles::color(styles::MegatoyCol::TextMuted),
                       "No folders added yet.");
  }

  std::optional<std::filesystem::path> folder_to_remove;
  std::optional<std::pair<std::size_t, std::size_t>> reorder;

  for (std::size_t i = 0; i < folders.size(); ++i) {
    const auto &folder = folders[i];
    ImGui::PushID(static_cast<int>(i));

    ImGui::BeginDisabled(i == 0);
    if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
      reorder = {i, i - 1};
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(i + 1 >= folders.size());
    if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
      reorder = {i, i + 1};
    }
    ImGui::EndDisabled();

    if (!context.preferences.workspace_folder_is_protected(folder.path)) {
      ImGui::SameLine();
      const char *remove_label =
          megatoy::platform::is_web() ? "Delete" : "Remove";
      if (ImGui::Button(remove_label)) {
        folder_to_remove = folder.path;
      }
    }

    ImGui::SameLine();
    if (!folder.available) {
      ImGui::TextColored(styles::color(styles::MegatoyCol::StatusError),
                         "%s (missing)", folder.name.c_str());
    } else if (!folder.writable) {
      ImGui::TextColored(styles::color(styles::MegatoyCol::TextMuted),
                         "%s (read-only)", folder.name.c_str());
    } else {
      ImGui::TextUnformatted(folder.name.c_str());
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", folder.path.string().c_str());
    }

    // The first writable folder is where new patches land.
    if (folder.available && folder.writable &&
        context.preferences.workspace().default_save_folder() == folder.path) {
      ImGui::SameLine();
      ImGui::TextColored(styles::color(styles::MegatoyCol::TextHighlight),
                         "(default)");
    }

    ImGui::PopID();
  }

  ImGui::Spacing();
  if (ImGui::Button("Add Folder...")) {
    context.open_add_folder_dialog = true;
  }

  bool show_presets = context.preferences.show_builtin_presets();
  if (ImGui::Checkbox("Show built-in presets", &show_presets)) {
    context.preferences.set_show_builtin_presets(show_presets);
    if (context.sync_workspace) {
      context.sync_workspace();
    }
  }

  if (folder_to_remove) {
    if (context.remove_workspace_folder) {
      context.remove_workspace_folder(*folder_to_remove);
    }
  } else if (reorder) {
    context.preferences.reorder_workspace_folder(reorder->first,
                                                 reorder->second);
    if (context.sync_workspace) {
      context.sync_workspace();
    }
  }

  // After the folders rather than before them: Sources is what the tab is
  // named for, and pushing the list down for one slider would bury it.
  ImGui::SeparatorText("Editor");

  auto &ui_prefs = context.ui_prefs;
  int reference_note = std::clamp(ui_prefs.envelope_reference_midi_note,
                                  ui::envelope::kMinReferenceMidiNote,
                                  ui::envelope::kMaxReferenceMidiNote);
  // A slider over MIDI numbers showing the note it lands on, the same way the
  // keyboard's octave slider shows the range it covers.
  const std::string note_name =
      ym2612::Note::from_midi_note(static_cast<uint8_t>(reference_note)).name();
  if (ImGui::SliderInt("Envelope reference note", &reference_note,
                       ui::envelope::kMinReferenceMidiNote,
                       ui::envelope::kMaxReferenceMidiNote,
                       note_name.c_str())) {
    ui_prefs.envelope_reference_midi_note = reference_note;
  }
  ImGui::TextWrapped(
      "The note the operator envelope graphs are drawn at. Only key scaling "
      "makes an envelope depend on pitch.");
}

void render_sound_tab(PreferencesContext &context) {
  auto &ui_prefs = context.ui_prefs;

  static constexpr const char *chip_types[] = {
      "YM2612 (Model 1, DAC distortion)", "YM3438 (Model 2, clean)"};
  ui_prefs.ym2612_chip_type = std::clamp(ui_prefs.ym2612_chip_type, 0, 1);
  ImGui::Combo("Chip", &ui_prefs.ym2612_chip_type, chip_types,
               static_cast<int>(std::size(chip_types)));

  ImGui::Spacing();

  // The frames the device ends up with. The web backend is asked for half of
  // it, because it doubles whatever it is given, so the default is not the
  // same number on every platform.
  static constexpr int kBufferChoices[] = {256, 384, 512, 1024, 2048};
  const int platform_default = context.default_audio_buffer_frames;
  std::vector<std::string> buffer_labels;
  std::vector<const char *> buffer_label_pointers;
  buffer_labels.reserve(std::size(kBufferChoices));
  for (const int frames : kBufferChoices) {
    buffer_labels.push_back(std::to_string(frames) + " frames" +
                            (frames == platform_default ? " (default)" : ""));
  }
  for (const auto &label : buffer_labels) {
    buffer_label_pointers.push_back(label.c_str());
  }

  const int selected_frames = ui_prefs.audio_buffer_frames > 0
                                  ? ui_prefs.audio_buffer_frames
                                  : platform_default;
  int buffer_index = 0;
  for (int i = 0; i < static_cast<int>(std::size(kBufferChoices)); ++i) {
    if (kBufferChoices[i] == selected_frames) {
      buffer_index = i;
      break;
    }
  }
  if (ImGui::Combo("Buffer size", &buffer_index, buffer_label_pointers.data(),
                   static_cast<int>(buffer_label_pointers.size()))) {
    ui_prefs.audio_buffer_frames = kBufferChoices[buffer_index];
  }
  ImGui::TextWrapped("Lower values reduce latency but increase the risk of "
                     "audio dropouts. Changes apply on the next launch.");

  ImGui::Spacing();

  // Not a MIDI setting: the channel allocator runs the same whichever
  // keyboard the note arrived from.
  ImGui::Checkbox("Steal oldest note when all 6 channels are busy",
                  &ui_prefs.steal_oldest_note_when_full);
}

// MIDI and the typing keyboard share a tab because they answer the same
// question -- how notes get in -- and neither fills one on its own.
void render_input_tab(PreferencesContext &context) {
  auto &ui_prefs = context.ui_prefs;

  ImGui::SeparatorText("MIDI");

  ImGui::Checkbox("Use MIDI velocity", &ui_prefs.use_velocity);
  if (!ui_prefs.use_velocity) {
    ImGui::TextWrapped("Notes play at full velocity.");
  }
  ImGui::SliderInt("Velocity sensitivity", &ui_prefs.velocity_sensitivity_depth,
                   0, 100, "%d%%");

  ImGui::Checkbox("Use pitch bend", &ui_prefs.use_pitch_bend);
  ImGui::Checkbox("Use mod wheel (vibrato)", &ui_prefs.use_mod_wheel);

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
  const auto selected_layout = clamp_layout_pref(ui_prefs.midi_keyboard_layout);
  if (selected_layout == TypingKeyboardLayout::Custom) {
    ImGui::Spacing();
    render_custom_layout_editor(ui_prefs);
  }
}

} // namespace

void render_preferences_window(const char *title, PreferencesContext &context) {
  auto &ui_prefs = context.ui_prefs;
  normalize_custom_layout(ui_prefs);
  if (!ui_prefs.show_preferences) {
    return;
  }

  ImGui::SetNextWindowSize(ui::scale::px(ImVec2(480, 260)),
                           ImGuiCond_FirstUseEver);

  if (ImGui::Begin(title, &ui_prefs.show_preferences)) {
    if (ImGui::BeginTabBar("##preference_tabs")) {
      // Each tab scrolls in its own child so the tab bar stays put. Input in
      // particular is taller than the window once a few devices show up.
      if (ImGui::BeginTabItem("General")) {
        ImGui::BeginChild("##general");
        render_general_tab(context);
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
      if (context.allow_workspace_ui && ImGui::BeginTabItem("Patches")) {
        ImGui::BeginChild("##patches");
        render_patches_tab(context);
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Sound")) {
        ImGui::BeginChild("##sound");
        render_sound_tab(context);
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Input")) {
        ImGui::BeginChild("##input");
        render_input_tab(context);
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
  }

  ImGui::End();

  // The flag is consumed by the main menu, which owns the single folder
  // picker so the two entry points cannot open it twice.
}
} // namespace ui
