#include "preferences.hpp"
#include "audio/load_meter.hpp"
#include "gui/app_window.hpp"
#include "gui/areas.hpp"
#include "gui/envelope/envelope_curve.hpp"
#include "gui/input/key_name_utils.hpp"
#include "gui/input/typing_keyboard_layout.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/styles/theme.hpp"
#include "gui/ui_scale.hpp"
#include "platform/platform_config.hpp"
#include "side_list.hpp"
#include "view_patch_drop.hpp"
#include "ym2612/note.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>
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
  // Keyboard keys only: the click on Cancel, or any other, is a key to
  // ImGui too.
  for (int key = ImGuiKey_Keyboard_BEGIN; key < ImGuiKey_Keyboard_END; ++key) {
    ImGuiKey imgui_key = static_cast<ImGuiKey>(key);
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

/// A key's name as it is shown on the piano: short, and a dash for none.
std::string key_caption(ImGuiKey key) {
  if (key == ImGuiKey_None) {
    return "-";
  }
  std::string label = short_key_name(key);
  if (label.empty() || label == "??") {
    const char *fallback = ImGui::GetKeyName(key);
    label = fallback ? fallback : "??";
  }
  return label;
}

bool is_black_slot(std::size_t slot) {
  switch (slot % 12) {
  case 1:
  case 3:
  case 6:
  case 8:
  case 10:
    return true;
  default:
    return false;
  }
}

/// Whether a click or a long press asked to clear the item just drawn.
bool clear_requested() {
  return ImGui::IsItemHovered() &&
         ImGui::IsMouseReleased(ImGuiMouseButton_Right);
}

/**
 * The layout's slots as piano keys from C, each showing the keyboard key that
 * plays it: "?" while it waits for one, a dash for none, and in the warning
 * colour when another slot has the same key. A click starts or stops waiting;
 * a right click (a long press on a touch screen) clears the slot.
 */
void render_layout_piano(CustomLayoutEditorState &state,
                         std::vector<int> &slots,
                         const std::map<ImGuiKey, int> &usage) {
  const std::size_t count = slots.size();
  std::size_t whites = 0;
  for (std::size_t slot = 0; slot < count; ++slot) {
    whites += is_black_slot(slot) ? 0 : 1;
  }
  const ImGuiStyle &style = ImGui::GetStyle();
  const float beside = ImGui::GetFrameHeight() + style.ItemSpacing.x;
  const float white_w = std::clamp((ImGui::GetContentRegionAvail().x - beside) /
                                       static_cast<float>(whites),
                                   ui::scale::px(20.0f), ui::scale::px(40.0f));
  const float white_h = ui::scale::px(110.0f);
  const float black_w = std::floor(white_w * 0.64f);
  const float black_h = std::floor(white_h * 0.6f);
  const float caption_h = ImGui::GetTextLineHeight();

  struct Slot {
    ImRect rect;
    std::size_t index;
    bool black;
  };
  std::vector<Slot> rects;
  rects.reserve(count);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  float x = origin.x;
  for (std::size_t slot = 0; slot < count; ++slot) {
    if (is_black_slot(slot)) {
      const float left = std::floor(x - black_w * 0.5f);
      rects.push_back(
          {ImRect(left, origin.y, left + black_w, origin.y + black_h), slot,
           true});
    } else {
      const float left = std::floor(x);
      rects.push_back(
          {ImRect(left, origin.y, std::floor(x + white_w), origin.y + white_h),
           slot, false});
      x += white_w;
    }
  }
  const float width = std::max(x, rects.back().rect.Max.x) - origin.x;

  ImGui::InvisibleButton("##piano", ImVec2(width, white_h + caption_h));
  const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
  const bool clearing = clear_requested();
  if (clicked || clearing) {
    // Black keys sit on top of the white ones.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const Slot *hit = nullptr;
    for (const auto &slot : rects) {
      if (slot.rect.Contains(mouse) && (hit == nullptr || slot.black)) {
        hit = &slot;
      }
    }
    if (hit != nullptr) {
      const int index = static_cast<int>(hit->index);
      if (clearing) {
        slots[hit->index] = static_cast<int>(ImGuiKey_None);
      } else {
        state.slot_capture_index =
            state.slot_capture_index == index ? -1 : index;
        state.octave_capture_target =
            CustomLayoutEditorState::OctaveCaptureTarget::None;
      }
    }
  }

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const ImU32 border = styles::color_u32(styles::MegatoyCol::PianoKeyBorder);
  const auto draw_slot = [&](const Slot &slot) {
    const bool waiting =
        state.slot_capture_index == static_cast<int>(slot.index);
    const ImGuiKey key = key_from_preference(slots[slot.index], ImGuiKey_None);
    const auto used = usage.find(key);
    const bool duplicate = used != usage.end() && used->second > 1;
    using Col = styles::MegatoyCol;
    const Col fill =
        slot.black ? (waiting ? Col::PianoBlackKeyPressed : Col::PianoBlackKey)
                   : (waiting ? Col::PianoWhiteKeyPressed : Col::PianoWhiteKey);
    if (slot.black) {
      // Over the white keys, so the colour, translucent as the soft
      // keyboard's, goes on the window's background rather than on them.
      draw_list->AddRectFilled(slot.rect.Min, slot.rect.Max,
                               ImGui::GetColorU32(ImGuiCol_WindowBg));
    }
    draw_list->AddRectFilled(slot.rect.Min, slot.rect.Max,
                             styles::color_u32(fill));
    draw_list->AddRect(slot.rect.Min,
                       ImVec2(slot.rect.Max.x + 1.0f, slot.rect.Max.y), border);
    const std::string caption = waiting ? "?" : key_caption(key);
    const Col text = duplicate               ? Col::StatusWarning
                     : waiting || slot.black ? Col::TextOnBlackKey
                                             : Col::TextOnWhiteKey;
    const ImVec2 size = ImGui::CalcTextSize(caption.c_str());
    draw_list->AddText(
        ImVec2(slot.rect.Min.x + (slot.rect.GetWidth() - size.x) * 0.5f,
               slot.rect.Max.y - size.y - style.FramePadding.y),
        styles::color_u32(text), caption.c_str());
  };
  for (const auto &slot : rects) {
    if (!slot.black) {
      draw_slot(slot);
      if (slot.index % 12 == 0) {
        const std::string octave =
            slot.index == 0 ? "C" : "C+" + std::to_string(slot.index / 12);
        draw_list->AddText(
            ImVec2(slot.rect.Min.x + style.FramePadding.x, slot.rect.Max.y),
            styles::color_u32(styles::MegatoyCol::TextMuted), octave.c_str());
      }
    }
  }
  for (const auto &slot : rects) {
    if (slot.black) {
      draw_slot(slot);
    }
  }
}

/// An octave key's button: its key, "?" while waiting for one. A click starts
/// or stops waiting; a right click clears it.
void octave_key_button(const char *id, int &stored_key,
                       CustomLayoutEditorState &state,
                       CustomLayoutEditorState::OctaveCaptureTarget target) {
  const bool waiting = state.octave_capture_target == target;
  const std::string caption =
      waiting ? "?"
              : key_caption(key_from_preference(stored_key, ImGuiKey_None));
  if (waiting) {
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
  }
  const bool clicked =
      ImGui::Button((caption + "##" + id).c_str(),
                    ImVec2(ImGui::GetFrameHeight() * 1.6f, 0.0f));
  if (waiting) {
    ImGui::PopStyleColor();
  }
  if (clicked) {
    state.octave_capture_target =
        waiting ? CustomLayoutEditorState::OctaveCaptureTarget::None : target;
    state.slot_capture_index = -1;
  }
  if (clear_requested()) {
    stored_key = static_cast<int>(ImGuiKey_None);
  }
}

void render_custom_layout_editor(UIPreferences &prefs) {
  auto &state = custom_layout_editor_state();
  normalize_custom_layout(prefs);
  clamp_capture_state(state, prefs);
  poll_custom_layout_capture(state, prefs);

  ImGui::TextColored(styles::color(styles::MegatoyCol::TextMuted),
                     "Click a key, then press the keyboard key to put on it. "
                     "Right-click clears it.");

  auto &slots = prefs.custom_typing_layout_keys;
  std::map<ImGuiKey, int> usage;
  for (const auto value : slots) {
    if (const ImGuiKey key = key_from_preference(value, ImGuiKey_None);
        key != ImGuiKey_None) {
      ++usage[key];
    }
  }

  render_layout_piano(state, slots, usage);

  // Slots are added and taken away at the top end.
  ImGui::SameLine();
  ImGui::BeginGroup();
  const ImVec2 square(ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
  ImGui::BeginDisabled(slots.size() >= ui::typing_layout_max_custom_slots);
  if (ImGui::Button("+", square)) {
    slots.push_back(static_cast<int>(ImGuiKey_None));
  }
  ImGui::EndDisabled();
  ImGui::BeginDisabled(slots.size() <= ui::typing_layout_min_custom_slots);
  if (ImGui::Button("-", square)) {
    slots.pop_back();
    clamp_capture_state(state, prefs);
  }
  ImGui::EndDisabled();
  ImGui::EndGroup();

  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Octave");
  ImGui::SameLine();
  ImGui::TextColored(styles::color(styles::MegatoyCol::TextMuted), "Down");
  ImGui::SameLine();
  octave_key_button("octave_down", prefs.custom_typing_octave_down_key, state,
                    CustomLayoutEditorState::OctaveCaptureTarget::Down);
  ImGui::SameLine();
  ImGui::TextColored(styles::color(styles::MegatoyCol::TextMuted), "Up");
  ImGui::SameLine();
  octave_key_button("octave_up", prefs.custom_typing_octave_up_key, state,
                    CustomLayoutEditorState::OctaveCaptureTarget::Up);

  if (ImGui::Button("Reset to QWERTY")) {
    copy_builtin_to_preferences(prefs.custom_typing_layout_keys,
                                qwerty_typing_layout_keys);
    normalize_custom_layout(prefs);
  }
  ImGui::SameLine();
  if (ImGui::Button("Reset to AZERTY")) {
    copy_builtin_to_preferences(prefs.custom_typing_layout_keys,
                                azerty_typing_layout_keys);
    normalize_custom_layout(prefs);
  }

  std::string duplicates;
  for (const auto &[key, uses] : usage) {
    if (uses > 1) {
      duplicates += (duplicates.empty() ? "" : ", ") + key_caption(key);
    }
  }
  if (!duplicates.empty()) {
    ImGui::TextColored(styles::color(styles::MegatoyCol::StatusWarning),
                       "On more than one key: %s", duplicates.c_str());
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

/// A section's title, a line's height below the section before it.
void section_title(const char *title, bool first = false) {
  if (!first) {
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
  }
  ImGui::SeparatorText(title);
}

void render_general_page(PreferencesContext &context) {
  auto &prefs = context.preferences;
  auto &ui_prefs = context.ui_prefs;

  section_title("Appearance", true);
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

  section_title("Editing");
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
void render_patches_page(PreferencesContext &context) {
  section_title("Sources", true);
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

  // After the folders rather than before them: Sources is what the page is
  // named for, and pushing the list down for one slider would bury it.
  section_title("Editor");

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

void render_sound_page(PreferencesContext &context) {
  auto &ui_prefs = context.ui_prefs;

  section_title("Emulation", true);
  static constexpr const char *cores[] = {"Nuked-OPN2 (accurate)",
                                         "ymfm (fast)"};
  ui_prefs.ym2612_core = std::clamp(ui_prefs.ym2612_core, 0, 1);
  ImGui::Combo("Core", &ui_prefs.ym2612_core, cores,
               static_cast<int>(std::size(cores)));

  static constexpr const char *chip_types[] = {
      "YM2612 (Model 1, DAC distortion)", "YM3438 (Model 2, clean)"};
  ui_prefs.ym2612_chip_type = std::clamp(ui_prefs.ym2612_chip_type, 0, 1);
  ImGui::Combo("Chip", &ui_prefs.ym2612_chip_type, chip_types,
               static_cast<int>(std::size(chip_types)));

  section_title("Audio");
  // The frames the device ends up with. The web backend is asked for half of
  // it, because it doubles whatever it is given, so the default is not the
  // same number on every platform.
  const int platform_default = context.default_audio_buffer_frames;
  std::vector<std::string> buffer_labels;
  std::vector<const char *> buffer_label_pointers;
  buffer_labels.reserve(std::size(kAudioBufferChoices));
  for (const int frames : kAudioBufferChoices) {
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
  for (int i = 0; i < static_cast<int>(std::size(kAudioBufferChoices)); ++i) {
    if (kAudioBufferChoices[i] == selected_frames) {
      buffer_index = i;
      break;
    }
  }
  if (ImGui::Combo("Buffer size", &buffer_index, buffer_label_pointers.data(),
                   static_cast<int>(buffer_label_pointers.size()))) {
    ui_prefs.audio_buffer_frames = kAudioBufferChoices[buffer_index];
  }
  ImGui::TextWrapped("Lower values reduce latency but increase the risk of "
                     "audio dropouts.");

  ImGui::Spacing();

  static constexpr const char *load_readings[] = {"Peak", "Peak and average"};
  ui_prefs.audio_load_reading = static_cast<int>(
      audio::load_reading_from_int(ui_prefs.audio_load_reading));
  ImGui::Combo("Load meter", &ui_prefs.audio_load_reading, load_readings,
               static_cast<int>(std::size(load_readings)));

  section_title("Channels");
  // Not a MIDI setting: the channel allocator runs the same whichever
  // keyboard the note arrived from.
  ImGui::Checkbox("Steal oldest note when all 6 channels are busy",
                  &ui_prefs.steal_oldest_note_when_full);
}

// MIDI and the typing keyboard share a page because they answer the same
// question -- how notes get in -- and neither fills one on its own.
void render_input_page(PreferencesContext &context) {
  auto &ui_prefs = context.ui_prefs;

  section_title("MIDI", true);

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

  section_title("Typing Keyboard");

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
  const bool open_sound_tab = std::exchange(context.open_sound_tab, false);
  if (open_sound_tab) {
    ui_prefs.show_preferences = true;
  }
  if (!ui_prefs.show_preferences) {
    return;
  }

  ImGui::SetNextWindowSize(ui::scale::px(ImVec2(480, 260)),
                           ImGuiCond_FirstUseEver);
  if (open_sound_tab) {
    ImGui::SetNextWindowFocus();
  }

  areas::keep_in_area(areas::Area::Main, title);
  if (ui::begin_window(title, &ui_prefs.show_preferences)) {
    static constexpr const char *kPages[] = {"General", "Patches", "Sound",
                                             "Input"};
    constexpr int kSoundPage = 2;
    const int page =
        side_list("##preference_pages", kPages, ui::scale::px(140.0f),
                  open_sound_tab ? kSoundPage : -1);
    // Each page scrolls on its own, beside the list, and keeps its own place.
    // Input in particular is taller than the window once a few devices show
    // up.
    ImGui::PushID(page);
    if (ImGui::BeginChild("##preference_page")) {
      switch (page) {
      case 0:
        render_general_page(context);
        break;
      case 1:
        render_patches_page(context);
        break;
      case kSoundPage:
        render_sound_page(context);
        break;
      default:
        render_input_page(context);
        break;
      }
    }
    ImGui::EndChild();
    ImGui::PopID();
  }

  view_patch_drop_target();
  ImGui::End();

  // The flag is consumed by the main menu, which owns the single folder
  // picker so the two entry points cannot open it twice.
}
} // namespace ui
