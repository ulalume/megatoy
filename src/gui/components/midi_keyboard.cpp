#include "midi_keyboard.hpp"
#include "gui/app_window.hpp"

#include "core/types.hpp"
#include "gui/areas.hpp"
#include "gui/components/menu_rows.hpp"
#include "gui/fonts/megatoy_icons.hpp"
#include "gui/frame_requests.hpp"
#include "gui/input/key_name_utils.hpp"
#include "gui/input/keyboard_typing.hpp"
#include "gui/input/touch.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "util.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ui {
namespace {

constexpr float playback_step_duration = 0.35f;

/// The chromatic keyboard's narrowest white key, and its black keys' width
/// and height against a white key's.
constexpr float kMinWhiteKeyWidth = 24.0f;
constexpr float kBlackKeyWidth = 0.6f;
constexpr float kBlackKeyHeight = 0.6f;

/// The families each column of the scale list starts at, for one to three
/// columns, so the columns stand about as tall as each other. Chromatic
/// stands on a row of its own above them.
constexpr int kScaleColumnGroups[3][3] = {{0}, {0, 3}, {0, 2, 4}};

/// The scales from `first` to `last` of the list, each family under its
/// name, as rows `width` wide, or across the list for 0. Returns whether one
/// was picked into `scale`.
bool scale_rows(std::size_t first, std::size_t last, Scale &scale,
                float width) {
  const auto &infos = scale_infos();
  bool picked = false;
  int group = -1;
  for (std::size_t i = first; i < last; ++i) {
    const ScaleInfo &info = infos[i];
    if (info.group != group && info.group >= 0) {
      ImGui::SeparatorText(kScaleGroups[info.group]);
    }
    group = info.group;
    const std::string label = scale_label(info);
    const bool selected = info.scale == scale;
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2(width, 0.0f))) {
      scale = info.scale;
      picked = true;
    }
    if (selected) {
      ImGui::SetItemDefaultFocus();
    }
    ImGui::PopID();
  }
  return picked;
}

/**
 * The scale list: Chromatic on a row of its own, then each family under its
 * name, in as many columns as the viewport has room for, up to three.
 * Returns whether a scale was picked into `scale`.
 */
bool scale_combo(const char *id, Scale &scale) {
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  if (!begin_combo(id, scale_name(scale), 0, viewport->WorkSize.y * 0.9f)) {
    return false;
  }
  const auto &infos = scale_infos();
  // Every row as wide as the widest name.
  float width = 0.0f;
  for (const ScaleInfo &info : infos) {
    width = std::max(width, ImGui::CalcTextSize(scale_label(info).c_str()).x);
  }
  const ImGuiStyle &style = ImGui::GetStyle();
  const float column = width + style.ItemSpacing.x;
  const int columns = std::clamp(
      static_cast<int>((viewport->WorkSize.x - style.WindowPadding.x * 2.0f) /
                       column),
      1, 3);
  // Where each column starts in the list.
  std::vector<std::size_t> starts;
  for (int c = 0; c < columns; ++c) {
    const int group = kScaleColumnGroups[columns - 1][c];
    const auto at = std::find_if(
        infos.begin(), infos.end(),
        [group](const ScaleInfo &info) { return info.group == group; });
    starts.push_back(static_cast<std::size_t>(at - infos.begin()));
  }
  starts.push_back(infos.size());
  // Chromatic, across the list.
  bool picked = scale_rows(0, starts.front(), scale, 0.0f);
  if (columns == 1) {
    picked |= scale_rows(starts.front(), infos.size(), scale, width);
  } else {
    ImGui::PushStyleVarX(ImGuiStyleVar_CellPadding, style.ItemSpacing.x * 0.5f);
    if (ImGui::BeginTable("##scale_columns", columns,
                          ImGuiTableFlags_SizingFixedFit)) {
      for (int c = 0; c < columns; ++c) {
        ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed,
                                width);
      }
      for (int c = 0; c < columns; ++c) {
        ImGui::TableNextColumn();
        picked |=
            scale_rows(starts[static_cast<std::size_t>(c)],
                       starts[static_cast<std::size_t>(c) + 1], scale, width);
      }
      ImGui::EndTable();
    }
    ImGui::PopStyleVar();
  }
  end_combo();
  return picked;
}

std::vector<ym2612::Note>
build_playback_sequence(const std::vector<Key> &scale_keys,
                        uint8_t base_octave) {
  std::vector<ym2612::Note> sequence;
  if (scale_keys.empty()) {
    return sequence;
  }
  std::vector<Key> keys_with_top = scale_keys;
  keys_with_top.push_back(scale_keys.front());
  int current_octave = std::clamp<int>(base_octave, 0, 7);
  int prev_key_value = -1;
  for (const auto key : keys_with_top) {
    const int key_value = static_cast<int>(key);
    if (prev_key_value >= 0 && key_value < prev_key_value) {
      current_octave = std::min(current_octave + 1, 7);
    }
    sequence.push_back(ym2612::Note{
        static_cast<uint8_t>(std::clamp(current_octave, 0, 7)), key});
    prev_key_value = key_value;
  }
  return sequence;
}

/// Lets go of the keys the mouse and fingers hold, for a frame the keys are
/// not drawn in and so cannot see them lift.
void release_held_keys(MidiKeyboardContext &context) {
  for (const auto &note : context.state.held_notes) {
    if (context.key_off) {
      context.key_off(note);
    }
  }
  context.state.held_notes.clear();
  context.state.fingers_on_keyboard.clear();
}

void stop_playback(MidiKeyboardContext &context) {
  auto &playback = context.state.playback;
  if (playback.note_active && context.key_off) {
    context.key_off(playback.current_note);
  }
  playback.note_active = false;
  playback.playing = false;
  playback.timer = 0.0f;
  playback.next_index = 0;
  playback.sequence_snapshot.clear();
}

bool can_play_scale(const MidiKeyboardContext &context) {
  return !context.state.playback_sequence.empty() && context.key_on &&
         context.key_off;
}

/// The scale the keyboard is set to, as "C Major" or "Chromatic".
std::string scale_title(const InputState &input) {
  const auto &settings = input.midi_keyboard_settings;
  const char *scale = scale_name(settings.scale);
  if (settings.scale == Scale::CHROMATIC) {
    return scale;
  }
  return std::string(key_names[static_cast<int>(settings.key)]) + " " + scale;
}

void start_playback(MidiKeyboardContext &context) {
  if (!can_play_scale(context)) {
    return;
  }
  stop_playback(context);
  context.state.playback.sequence_snapshot = context.state.playback_sequence;
  context.state.playback.playing = true;
  context.state.playback.timer = 0.0f;
  context.state.playback.next_index = 0;
}

void update_playback(MidiKeyboardContext &context, float delta_time) {
  auto &playback = context.state.playback;
  const auto &sequence = playback.sequence_snapshot;
  if (!playback.playing) {
    return;
  }
  if (sequence.empty() || !context.key_on || !context.key_off) {
    stop_playback(context);
    return;
  }
  // The next note is due on time, not at the next input.
  keep_drawing();

  playback.timer -= delta_time;
  if (playback.timer > 0.0f) {
    return;
  }

  if (playback.note_active) {
    context.key_off(playback.current_note);
    playback.note_active = false;
  }

  const ym2612::Note &next_note = sequence[playback.next_index];
  context.key_on(next_note, 127);
  playback.current_note = next_note;
  playback.note_active = true;

  playback.next_index = (playback.next_index + 1) % sequence.size();
  playback.timer += playback_step_duration;
  if (playback.timer < 0.0f) {
    playback.timer = playback_step_duration;
  }
}

bool refresh_state(MidiKeyboardState &state, const InputState &input,
                   const std::vector<TypingKeys> &keyboards) {
  const auto &settings = input.midi_keyboard_settings;
  if (state.initialized && state.cached_scale == settings.scale &&
      state.cached_key == settings.key &&
      state.cached_octave == input.keyboard_typing_octave &&
      state.cached_upper_octave == input.keyboard_typing_upper_octave &&
      state.cached_keyboards == keyboards) {
    return false;
  }

  state.cached_scale = settings.scale;
  state.cached_key = settings.key;
  state.cached_octave = input.keyboard_typing_octave;
  state.cached_upper_octave = input.keyboard_typing_upper_octave;
  state.cached_keyboards = keyboards;

  state.key_mappings.clear();
  state.typed_keyboards.clear();
  const std::array<uint8_t, 2> octaves{input.keyboard_typing_octave,
                                       input.keyboard_typing_upper_octave};
  for (std::size_t i = 0; i < keyboards.size(); ++i) {
    const auto mapping = create_key_mappings(settings.scale, settings.key,
                                             octaves[i], keyboards[i].places);
    auto &typed = state.typed_keyboards.emplace_back();
    for (const auto &[key, note] : mapping) {
      typed.keys[note] = key;
    }
    if (!typed.keys.empty()) {
      typed.range = typed.keys.begin()->first.name() + "-" +
                    typed.keys.rbegin()->first.name();
    }
    state.key_mappings.insert(mapping.begin(), mapping.end());
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

  state.playback_sequence =
      build_playback_sequence(state.scale_keys, input.keyboard_typing_octave);
  state.typing_range_moved = true;
  state.initialized = true;
  return true;
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
  auto &ui_prefs = context.ui_prefs;
  const auto keyboards = typing_keyboards(ui_prefs);

  const auto clamp_scale = [](int value) {
    return std::clamp(value, 0, kScaleCount - 1);
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
  input.keyboard_typing_octave = static_cast<uint8_t>(pref_octave);
  input.keyboard_typing_upper_octave = static_cast<uint8_t>(
      std::clamp(context.ui_prefs.midi_keyboard_typing_upper_octave, 0, 7));

  refresh_state(context.state, input, keyboards);
  const auto &key_mappings = context.state.key_mappings;

  update_playback(context, ImGui::GetIO().DeltaTime);

  // A key pressed with Command, Control or Option is a shortcut's, such as
  // Command+S, and plays nothing.
  const ImGuiIO &io = ImGui::GetIO();
  const bool shortcut = io.KeyCtrl || io.KeySuper || io.KeyAlt;
  if (!shortcut && !io.WantTextInput && !key_mappings.empty()) {
    KeyboardTypingContext typing_context{
        context.input_state, keyboards[0].octave.first,
        keyboards[0].octave.second,
        [&context](ym2612::Note note, uint8_t velocity) {
          context.key_on(note, velocity);
        },
        [&context](ym2612::Note note) { context.key_off(note); }};
    if (keyboards.size() > 1) {
      typing_context.upper_octave_down_key = keyboards[1].octave.first;
      typing_context.upper_octave_up_key = keyboards[1].octave.second;
    }
    check_keyboard_typing(typing_context, key_mappings);
  }
  context.ui_prefs.midi_keyboard_typing_octave =
      static_cast<int>(input.keyboard_typing_octave);
  context.ui_prefs.midi_keyboard_typing_upper_octave =
      static_cast<int>(input.keyboard_typing_upper_octave);

  if (!ui_prefs.show_midi_keyboard) {
    release_held_keys(context);
    return;
  }

  ImGui::SetNextWindowSize(ui::scale::px(ImVec2(400, 180)),
                           ImGuiCond_FirstUseEver);
  areas::keep_in_area(areas::Area::FooterRight, title);
  // Its contents run to the window's edges, as the waveform's do.
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  const bool visible = ui::begin_window(title, &ui_prefs.show_midi_keyboard);
  ImGui::PopStyleVar();
  if (!visible) {
    release_held_keys(context);
    ImGui::End();
    return;
  }

  // Key and scale read as one phrase, "C Major Scale".
  ImGui::BeginDisabled(keyboard_settings.scale == Scale::CHROMATIC);
  int current_key = static_cast<int>(keyboard_settings.key);
  ImGui::SetNextItemWidth(ui::scale::px(40.0f));
  if (ui::combo("##Key", &current_key, key_names, IM_ARRAYSIZE(key_names))) {
    keyboard_settings.key = static_cast<Key>(current_key);
    context.ui_prefs.midi_keyboard_key = current_key;
    refresh_state(context.state, input, keyboards);
  }
  ImGui::EndDisabled();

  ImGui::SameLine(0, ui::scale::px(1.0f));
  ImGui::SetNextItemWidth(ui::scale::px(150.0f));
  if (scale_combo("##Scale", keyboard_settings.scale)) {
    context.ui_prefs.midi_keyboard_scale =
        static_cast<int>(keyboard_settings.scale);
    if (keyboard_settings.scale == Scale::CHROMATIC) {
      keyboard_settings.key = Key::C;
      context.ui_prefs.midi_keyboard_key = static_cast<int>(Key::C);
    }
    refresh_state(context.state, input, keyboards);
  }

  // Each typed keyboard's octave, the lowest keyboard's first: a slider with
  // a button either side of it that moves it a step, the three a pixel
  // apart. They follow on the row, or take a row of their own when too
  // little is left.
  const std::array<std::pair<uint8_t *, int *>, 2> octaves{
      {{&input.keyboard_typing_octave,
        &context.ui_prefs.midi_keyboard_typing_octave},
       {&input.keyboard_typing_upper_octave,
        &context.ui_prefs.midi_keyboard_typing_upper_octave}}};
  const auto groups = static_cast<float>(keyboards.size());
  const float octave_gap = ui::scale::px(1.0f);
  const float group_gap = ImGui::GetStyle().ItemSpacing.x;
  const float buttons_width = (ImGui::GetFrameHeight() + octave_gap) * 2.0f;
  const float widest_slider = ui::scale::px(80.0f);
  ImGui::SameLine();
  if (ImGui::GetContentRegionAvail().x <
      (buttons_width + widest_slider) * groups + group_gap * (groups - 1.0f)) {
    ImGui::NewLine();
  }
  // Narrower only where the window has no room for it.
  const float slider_width =
      std::clamp(std::floor((ImGui::GetContentRegionAvail().x -
                             group_gap * (groups - 1.0f)) /
                                groups -
                            buttons_width),
                 ui::scale::px(24.0f), widest_slider);
  for (std::size_t i = 0; i < keyboards.size(); ++i) {
    if (i > 0) {
      ImGui::SameLine(0, group_gap);
    }
    const auto &typed = context.state.typed_keyboards;
    const char *range = i < typed.size() && !typed[i].range.empty()
                            ? typed[i].range.c_str()
                            : "Keys";
    const int octave = static_cast<int>(*octaves[i].first);
    int wanted = octave;
    // Each button names the key that does the same.
    const auto step_button = [&](const char *id, ImGuiDir direction, int step,
                                 const char *name, SDL_Scancode key) {
      const int to = octave + step;
      ImGui::BeginDisabled(to < 0 || to > 7);
      if (ImGui::ArrowButton(id, direction)) {
        wanted = to;
      }
      ImGui::EndDisabled();
      if (ImGui::BeginItemTooltip()) {
        ImGui::Text("%s: %s", name,
                    key == SDL_SCANCODE_UNKNOWN ? "Unassigned"
                                                : place_name(key).c_str());
        ImGui::EndTooltip();
      }
    };
    ImGui::PushID(static_cast<int>(i));
    step_button("##octave_down", ImGuiDir_Left, -1, "Octave down",
                keyboards[i].octave.first);
    ImGui::SameLine(0, octave_gap);
    ImGui::SetNextItemWidth(slider_width);
    ImGui::SliderInt("##Typing", &wanted, 0, 7, range);
    ImGui::SameLine(0, octave_gap);
    step_button("##octave_up", ImGuiDir_Right, 1, "Octave up",
                keyboards[i].octave.second);
    ImGui::PopID();
    if (wanted != octave) {
      *octaves[i].first = static_cast<uint8_t>(wanted);
      *octaves[i].second = wanted;
      refresh_state(context.state, input, keyboards);
    }
  }

  // Scrollable keyboard area, a pixel below the controls.
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() -
                       ImGui::GetStyle().ItemSpacing.y + ui::scale::px(1.0f));
  const ImVec2 available_region = ImGui::GetContentRegionAvail();

  if (ImGui::BeginChild("KeyboardScroll", ImVec2(0, available_region.y), false,
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    const auto &notes = context.state.display_notes;
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float key_height = ImGui::GetContentRegionAvail().y;
    const float y = cursor.y;

    struct KeyRect {
      ImRect rect;
      ym2612::Note note;
      bool white;
    };
    std::vector<KeyRect> key_rects;
    key_rects.reserve(notes.size());
    // The chromatic scale is laid out as a piano's: white keys side by side,
    // black keys shorter and narrower over the line between two. Any other
    // scale lays its notes side by side alike.
    const bool piano = keyboard_settings.scale == Scale::CHROMATIC;
    if (piano) {
      const auto whites = static_cast<float>(
          std::count_if(notes.begin(), notes.end(), [](const auto &note) {
            return ui::is_white_key(note.key);
          }));
      const bool black_first =
          !notes.empty() && !ui::is_white_key(notes.front().key);
      const bool black_last =
          !notes.empty() && !ui::is_white_key(notes.back().key);
      // A black key at either end stands half out past the white keys.
      const float ends =
          (black_first ? 0.5f : 0.0f) + (black_last ? 0.5f : 0.0f);
      const float min_white_width = ui::scale::px(kMinWhiteKeyWidth);
      // One pixel kept for the last key's right border.
      const float white_width =
          whites == 0.0f
              ? min_white_width
              : std::max(min_white_width, (available_region.x - 1.0f) /
                                              (whites + ends * kBlackKeyWidth));
      const float black_width = std::round(white_width * kBlackKeyWidth);
      const float black_height = std::round(key_height * kBlackKeyHeight);
      float x = cursor.x + (black_first ? black_width * 0.5f : 0.0f);
      for (const auto &note : notes) {
        if (ui::is_white_key(note.key)) {
          key_rects.push_back(
              {ImRect(std::floor(x), y, std::floor(x + white_width),
                      y + key_height),
               note, true});
          x += white_width;
        } else {
          const float left = std::floor(x - black_width * 0.5f);
          key_rects.push_back(
              {ImRect(left, y, left + black_width, y + black_height), note,
               false});
        }
      }
    } else {
      const float min_key_width = ui::scale::px(18.0f);
      // One pixel kept for the last key's right border.
      const float key_width =
          notes.empty() ? min_key_width
                        : std::max(min_key_width,
                                   (available_region.x - 1.0f) / notes.size());
      float x = cursor.x;
      for (const auto &note : notes) {
        // On whole pixels, so the borders do not blur across two.
        key_rects.push_back({ImRect(std::floor(x), y, std::floor(x + key_width),
                                    y + key_height),
                             note, ui::is_white_key(note.key)});
        x += key_width;
      }
    }
    // Black keys lie over white ones, so they take the pointer first.
    std::stable_partition(
        key_rects.begin(), key_rects.end(),
        [&](const KeyRect &key) { return !key.white && piano; });

    // A press on any key sounds whichever key the pointer is over, so a drag
    // plays each key it crosses. On a touch screen the fingers play, each its
    // own key, and the mouse SDL makes of the first one is left out.
    const bool by_touch =
        ImGui::GetIO().MouseSource == ImGuiMouseSource_TouchScreen;
    bool pointer_held = false;
    std::optional<ym2612::Note> under_pointer;
    for (const auto &key : key_rects) {
      ImGui::SetCursorScreenPos(key.rect.Min);
      ImGui::PushID(key.note.midi_note());
      ImGui::InvisibleButton("key", key.rect.GetSize());
      pointer_held |= ImGui::IsItemActive() && !by_touch;
      if (!under_pointer &&
          ImGui::IsItemHovered(
              ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        under_pointer = key.note;
      }
      ImGui::PopID();
    }

    // Each typed keyboard has a bar over its keys. Keyboards whose notes
    // run into one another have their bars one over another, the lowest
    // keyboard's undermost, as its row is on the computer's keyboard;
    // keyboards apart have theirs in one line.
    const auto &typed_keyboards = context.state.typed_keyboards;
    bool bars_stacked = false;
    for (std::size_t i = 0; i < typed_keyboards.size(); ++i) {
      for (std::size_t j = i + 1; j < typed_keyboards.size(); ++j) {
        const auto &a = typed_keyboards[i].keys;
        const auto &b = typed_keyboards[j].keys;
        bars_stacked |= !a.empty() && !b.empty() &&
                        !(a.rbegin()->first < b.begin()->first) &&
                        !(b.rbegin()->first < a.begin()->first);
      }
    }
    const float bar_rows =
        bars_stacked ? static_cast<float>(typed_keyboards.size()) : 1.0f;
    const float bar_height = ui::scale::px(bar_rows > 1.0f ? 3.0f : 4.0f);
    const float bar_step = bar_height + ui::scale::px(1.0f);
    const float bars_bottom = y + bar_step * bar_rows - ui::scale::px(1.0f);
    // Where each typed keyboard's keys begin and end.
    std::vector<std::pair<float, float>> typed_spans(typed_keyboards.size(),
                                                     {FLT_MAX, -FLT_MAX});
    // Drawn white keys first, so the black ones lie over them.
    for (const bool white_pass : {true, false}) {
      for (const auto &key : key_rects) {
        if (key.white != white_pass) {
          continue;
        }
        const auto &note = key.note;
        const ImVec2 key_min = key.rect.Min;
        const ImVec2 key_max = key.rect.Max;
        const float key_width = key_max.x - key_min.x;
        const bool is_white = key.white;
        const bool is_pressed =
            context.key_is_pressed ? context.key_is_pressed(note) : false;
        const ImU32 col =
            is_white ? (is_pressed ? white_key_pressed_col : white_key_col)
                     : (is_pressed ? black_key_pressed_col : black_key_col);
        if (!is_white && piano) {
          // Over the white keys, so the colour, translucent, goes on the
          // window's background rather than on them.
          draw_list->AddRectFilled(key_min, key_max,
                                   ImGui::GetColorU32(ImGuiCol_WindowBg));
        }
        draw_list->AddRectFilled(key_min, key_max, col);
        // One pixel further right, onto the next key's left edge, where that
        // key's own border draws over it again: neighbours share one line.
        draw_list->AddRect(key_min, ImVec2(key_max.x + 1.0f, key_max.y),
                           key_border_col, 0, 0, 1.0f);

        const ImU32 text_color =
            is_white ? white_key_text_col : black_key_text_col;
        // The key's typing names a little below the bars: below the black
        // keys on a white one that has them beside it. A key two keyboards
        // play has both names, the upper keyboard's uppermost.
        float text_top = bars_bottom + ui::scale::px(3.0f);
        if (is_white && piano) {
          text_top =
              key_min.y + key_height * kBlackKeyHeight + ui::scale::px(3.0f);
        }
        // How far down the typing names reach, to keep the note's name off
        // them.
        float key_name_bottom = key_min.y;
        for (std::size_t i = typed_keyboards.size(); i-- > 0;) {
          const auto mapped = typed_keyboards[i].keys.find(note);
          if (mapped == typed_keyboards[i].keys.end()) {
            continue;
          }
          auto &[span_min, span_max] = typed_spans[i];
          span_min = std::min(span_min, key_min.x);
          span_max = std::max(span_max, key_max.x);
          const std::string key_text = place_name(mapped->second);
          const ImVec2 text_size = ImGui::CalcTextSize(key_text.c_str());
          draw_list->AddText(
              ImVec2(key_min.x + (key_width - text_size.x) * 0.5f, text_top),
              text_color, key_text.c_str());
          key_name_bottom = text_top + text_size.y;
          text_top = key_name_bottom;
        }
        // The note's name at the key's foot, left out where the keys are too
        // short for it to clear the typing name.
        if (note.key == keyboard_settings.key) {
          const std::string key_text = note.name();
          const ImVec2 text_size = ImGui::CalcTextSize(key_text.c_str());
          const float top = key_max.y - text_size.y - 5.0f;
          if (top >= key_name_bottom + ui::scale::px(2.0f)) {
            draw_list->AddText(
                ImVec2(key_min.x + (key_width - text_size.x) * 0.5f, top),
                text_color, key_text.c_str());
          }
        }
      }
    }

    // The bars, each over the white and black keys alike.
    float typed_min = FLT_MAX;
    float typed_max = -FLT_MAX;
    for (std::size_t i = 0; i < typed_spans.size(); ++i) {
      auto [span_min, span_max] = typed_spans[i];
      if (span_min >= span_max) {
        continue;
      }
      typed_min = std::min(typed_min, span_min);
      typed_max = std::max(typed_max, span_max);
      float top = y;
      if (bars_stacked) {
        top += bar_step * static_cast<float>(typed_spans.size() - 1 - i);
      } else {
        // In one line, a bar stops short of the next keyboard's.
        for (const auto &[next_min, next_max] : typed_spans) {
          if (next_min < next_max && next_min > span_min) {
            span_max = std::min(span_max, next_min - ui::scale::px(2.0f));
          }
        }
      }
      draw_list->AddRectFilled(ImVec2(span_min, top),
                               ImVec2(span_max, top + bar_height),
                               white_key_pressed_col);
    }

    // Once the keys typing plays move, the keyboard scrolls to centre them
    // unless all of them are in view already.
    ImGuiWindow *keys_window = ImGui::GetCurrentWindow();
    if (context.state.typing_range_moved && typed_min < typed_max) {
      context.state.typing_range_moved = false;
      const ImRect &shown = keys_window->InnerRect;
      if (typed_min < shown.Min.x || typed_max > shown.Max.x) {
        ImGui::SetScrollFromPosX(
            (typed_min + typed_max) * 0.5f - keys_window->Pos.x, 0.5f);
        keep_drawing();
      }
    }

    std::vector<ym2612::Note> wanted;
    if (pointer_held && under_pointer) {
      wanted.push_back(*under_pointer);
    }
    const ImRect visible = keys_window->InnerClipRect;
    auto &on_keyboard = context.state.fingers_on_keyboard;
    std::erase_if(on_keyboard, [](const auto &entry) {
      const auto fingers = touch::fingers();
      return std::none_of(fingers.begin(), fingers.end(),
                          [&](const auto &f) { return f.id == entry.first; });
    });
    // A finger plays only if it came down on the keyboard itself, not on a
    // dialog or a window over it.
    const auto lands_here = [&visible](ImVec2 pos) {
      if (!visible.Contains(pos) || ImGui::GetTopMostPopupModal() != nullptr) {
        return false;
      }
      ImGuiWindow *hovered = nullptr;
      ImGuiWindow *under_moving = nullptr;
      ImGui::FindHoveredWindowEx(pos, false, &hovered, &under_moving);
      return hovered == ImGui::GetCurrentWindow();
    };
    for (const auto &finger : touch::fingers()) {
      const auto [entry, first_seen] =
          on_keyboard.try_emplace(finger.id, lands_here(finger.pos));
      if (!entry->second) {
        continue;
      }
      if (first_seen) {
        touch::claim(finger.id);
      }
      if (!visible.Contains(finger.pos)) {
        continue;
      }
      for (const auto &key : key_rects) {
        if (key.rect.Contains(finger.pos)) {
          if (std::find(wanted.begin(), wanted.end(), key.note) ==
              wanted.end()) {
            wanted.push_back(key.note);
          }
          break;
        }
      }
    }

    // Only what this keyboard turned on, and each of those let go for sure:
    // the chip's own list of sounding notes lags a frame behind, so a quick
    // slide would find a note it had just started not sounding yet.
    auto &held = context.state.held_notes;
    std::erase_if(held, [&](const ym2612::Note &note) {
      if (std::find(wanted.begin(), wanted.end(), note) != wanted.end()) {
        return false;
      }
      if (context.key_off) {
        context.key_off(note);
      }
      return true;
    });
    for (const auto &note : wanted) {
      // Not one another keyboard is already holding.
      if (std::find(held.begin(), held.end(), note) == held.end() &&
          !(context.key_is_pressed && context.key_is_pressed(note)) &&
          context.key_on) {
        context.key_on(note, 127);
        held.push_back(note);
      }
    }
  } else {
    release_held_keys(context);
  }
  ImGui::EndChild();

  ImGui::End();
};

namespace {

/// The switch's two faces at `fit`, under one ID so it stays the same item
/// as it toggles.
std::pair<const char *, const char *> scale_play_labels(BarCentreFit fit) {
  if (fit == BarCentreFit::Icon) {
    return {ICON_MEGATOY_PLAY "###scale_play",
            ICON_MEGATOY_STOP "###scale_play"};
  }
  return {ICON_MEGATOY_PLAY " Play###scale_play",
          ICON_MEGATOY_STOP " Stop###scale_play"};
}

float scale_play_switch_width(BarCentreFit fit) {
  const auto [play, stop] = scale_play_labels(fit);
  return std::max(ImGui::CalcTextSize(play, nullptr, true).x,
                  ImGui::CalcTextSize(stop, nullptr, true).x);
}

std::string scale_play_note(const MidiKeyboardContext &context) {
  return scale_title(context.input_state) + " Scale";
}

} // namespace

BarCentreSize scale_play_size(const MidiKeyboardContext &context,
                              BarCentreFit fit) {
  const float width = scale_play_switch_width(fit);
  if (fit != BarCentreFit::Full) {
    return {width, width};
  }
  return {width, width + ImGui::GetStyle().ItemSpacing.x +
                     ImGui::CalcTextSize(scale_play_note(context).c_str()).x};
}

void render_scale_play(MidiKeyboardContext &context, BarCentreFit fit,
                       float left, float right, float row_y) {
  const ImGuiStyle &style = ImGui::GetStyle();
  const auto [play, stop] = scale_play_labels(fit);
  const float width = scale_play_switch_width(fit);
  const float min_x = left + style.ItemSpacing.x;
  const float max_x = right - style.ItemSpacing.x;
  if (max_x - min_x < width) {
    return;
  }

  const bool playing = context.state.playback.playing;
  const float x = std::clamp(ImGui::GetWindowWidth() * 0.5f - width * 0.5f,
                             min_x, max_x - width);
  ImGui::SetCursorPos(ImVec2(x, row_y));
  ImGui::BeginDisabled(!playing && !can_play_scale(context));
  ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, bar_item_spacing());
  if (ImGui::Selectable(playing ? stop : play, false, ImGuiSelectableFlags_None,
                        ImVec2(width, 0.0f))) {
    if (playing) {
      stop_playback(context);
    } else {
      start_playback(context);
    }
  }
  ImGui::PopStyleVar();
  ImGui::EndDisabled();

  const std::string note = scale_play_note(context);
  if (fit == BarCentreFit::Full &&
      x + width + style.ItemSpacing.x + ImGui::CalcTextSize(note.c_str()).x <=
          max_x) {
    ImGui::SameLine();
    ImGui::TextColored(styles::color(styles::MegatoyCol::TextMuted), "%s",
                       note.c_str());
  } else if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip("%s", note.c_str());
  }
}

} // namespace ui
