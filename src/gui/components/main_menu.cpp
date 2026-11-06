#include "main_menu.hpp"
#include "gui/components/menu_rows.hpp"
#include "gui/components/named_slider.hpp"

#include "patches/save_controls.hpp"
#include "platform/platform_config.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>

#include "about_dialog.hpp"
#include "changelog_dialog.hpp"
#include "gui/components/common.hpp"
#include "gui/components/panel.hpp"
#include "gui/components/preferences.hpp"
#include "gui/image/app_icon.hpp"
#include "gui/save_export_actions.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "gui/window_title.hpp"
#include "ym2612/chip.hpp"
#include <IconsFontAwesome7.h>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>
#include <iterator>
#include <string>
#include <string_view>

namespace ui {

namespace {

// Full scale on the graph is the render deadline: 1.0 means a block took as
// long to produce as it takes to play.
constexpr float kLoadWarningLevel = 0.7f;
/// The graph's width beside the reading it holds.
constexpr float kLoadGraphWidth = 64.0f;
// How far back the graph reaches: long enough that a glitch is still on
// screen once the ear has noticed it and the eye has arrived.
constexpr float kLoadWindowMs = 10000.0f;
constexpr std::size_t kMaxLoadColumns = 256;

// The drawn window, folded to one column per pixel by taking the highest
// value in each. Peak, average and the dropout flags are taken over the
// samples the columns were folded from, so every reading describes the span
// the graph draws.
struct LoadWindow {
  std::array<float, kMaxLoadColumns> columns{};
  /// Set where any sample folded into the column broke the output.
  std::array<bool, kMaxLoadColumns> dropouts{};
  std::size_t count = 0;
  float latest = 0.0f;
  float peak = 0.0f;
  float average = 0.0f;
};

LoadWindow load_window(const audio::LoadMeter::History &history, float width) {
  LoadWindow out;
  if (history.count == 0) {
    return out;
  }
  const std::size_t wanted =
      history.slot_ms > 0.0f
          ? static_cast<std::size_t>(kLoadWindowMs / history.slot_ms) + 1
          : history.count;
  const std::size_t used = std::min(history.count, wanted);
  const std::size_t first = history.count - used;
  const std::size_t columns = std::min<std::size_t>(
      kMaxLoadColumns, std::max<std::size_t>(2, static_cast<std::size_t>(width)));
  out.count = std::min(columns, used);
  out.latest = history.values[history.count - 1];
  double total = 0.0;
  for (std::size_t i = 0; i < used; ++i) {
    const std::size_t column = out.count < 2 ? 0
                                             : i * (out.count - 1) / (used - 1);
    const float value = history.values[first + i];
    out.columns[column] = std::max(out.columns[column], value);
    out.dropouts[column] = out.dropouts[column] || audio::is_dropout(value);
    out.peak = std::max(out.peak, value);
    total += static_cast<double>(value);
  }
  out.average = static_cast<float>(total / static_cast<double>(used));
  return out;
}

// Alone the peak needs no label; paired it takes one, so neither number can be
// read for the other.
void format_load_reading(char *out, std::size_t size, const LoadWindow &window,
                         audio::LoadReading reading) {
  const double peak = static_cast<double>(window.peak) * 100.0;
  const double average = static_cast<double>(window.average) * 100.0;
  if (reading == audio::LoadReading::PeakAndAverage) {
    std::snprintf(out, size, "peak %.0f%%  avg %.0f%%", peak, average);
    return;
  }
  std::snprintf(out, size, "%.0f%%", peak);
}

void draw_load_graph(const Panel &panel, const LoadWindow &window) {
  const float width = panel.max.x - panel.min.x;
  const float height = panel.max.y - panel.min.y;

  const ImU32 warning = styles::color_u32(styles::MegatoyCol::StatusWarning);
  const ImU32 trace = ImGui::GetColorU32(ImGuiCol_PlotLines);

  const float threshold_y = panel.max.y - height * kLoadWarningLevel;
  panel.draw_list->AddLine(ImVec2(panel.min.x, threshold_y),
                           ImVec2(panel.max.x, threshold_y),
                           color_with_alpha(warning, 0.5f));

  if (window.count < 2) {
    return;
  }

  // The newest column sits on the right edge, older ones running left from it,
  // so a window that is not full yet grows leftwards instead of stretching.
  const float step = width / static_cast<float>(window.count - 1);

  // A dropout is a moment rather than a level, so it is marked across the
  // full height of the column it fell in and drawn under the trace.
  for (std::size_t i = 0; i < window.count; ++i) {
    if (!window.dropouts[i]) {
      continue;
    }
    const float x = panel.min.x + step * static_cast<float>(i);
    panel.draw_list->AddLine(ImVec2(x, panel.min.y), ImVec2(x, panel.max.y),
                             warning);
  }

  const auto point = [&](std::size_t index) {
    const float value = std::clamp(window.columns[index], 0.0f, 1.0f);
    const float x = panel.min.x + step * static_cast<float>(index);
    return ImVec2(x, panel.max.y - height * value);
  };
  const auto over_threshold = [&](std::size_t index) {
    return window.columns[index] > kLoadWarningLevel;
  };

  // Split into runs so a segment touching a sample over the threshold is
  // stroked in the warning colour.
  ImU32 run_color = (over_threshold(0) || over_threshold(1)) ? warning : trace;
  panel.draw_list->PathClear();
  panel.draw_list->PathLineTo(point(0));
  for (std::size_t i = 1; i < window.count; ++i) {
    const ImU32 color =
        (over_threshold(i - 1) || over_threshold(i)) ? warning : trace;
    if (color != run_color) {
      panel.draw_list->PathStroke(run_color, ImDrawFlags_None, 1.0f);
      panel.draw_list->PathClear();
      panel.draw_list->PathLineTo(point(i - 1));
      run_color = color;
    }
    panel.draw_list->PathLineTo(point(i));
  }
  panel.draw_list->PathStroke(run_color, ImDrawFlags_None, 1.0f);
}

/// The remedies for a high load. Each one that has nothing left to give is
/// disabled and says so.
/// The graph's readings in words, for its tooltip and the top of its menu.
void render_load_status(const LoadWindow &window) {
  ImGui::TextUnformatted("Sound Processing Load");
  ImGui::Text("  Now %.0f%%   Peak %.0f%%   Average %.0f%%",
              static_cast<double>(window.latest) * 100.0,
              static_cast<double>(window.peak) * 100.0,
              static_cast<double>(window.average) * 100.0);
}

void render_load_menu(MainMenuContext &context, const LoadWindow &window) {
  auto &ui_prefs = context.ui_prefs;

  render_load_status(window);
  ImGui::SeparatorText("Improve Performance");

  const bool core_at_best =
      ui_prefs.ym2612_core == static_cast<int>(ym2612::CoreType::Ymfm);
  if (ImGui::MenuItem(core_at_best ? "Switch Core to ymfm (Already Set)"
                                   : "Switch Core to ymfm...",
                      nullptr, false, !core_at_best)) {
    context.open_sound_preferences = true;
  }

  const int largest_buffer =
      kAudioBufferChoices[std::size(kAudioBufferChoices) - 1];
  const int buffer_frames = ui_prefs.audio_buffer_frames > 0
                                ? ui_prefs.audio_buffer_frames
                                : context.default_audio_buffer_frames;
  const bool buffer_at_best = buffer_frames >= largest_buffer;
  if (ImGui::MenuItem(buffer_at_best ? "Increase Buffer Size (Already Set)"
                                     : "Increase Buffer Size...",
                      nullptr, false, !buffer_at_best)) {
    context.open_sound_preferences = true;
  }

#if defined(MEGATOY_PLATFORM_WEB)
  // The browser draws the interface on the thread that renders the audio, so
  // the time the waveform takes comes out of the same deadline. Where audio
  // has a thread of its own it cannot, and the item is not offered.
  const bool waveform_at_best = !ui_prefs.show_waveform;
  if (ImGui::MenuItem(waveform_at_best ? "Hide Waveform (Already Set)"
                                       : "Hide Waveform",
                      nullptr, false, !waveform_at_best)) {
    ui_prefs.show_waveform = false;
  }
#endif
}

/**
 * The load reading at the right-hand end of the bar, inside its graph when
 * `graph_width` is more than 0 and alone otherwise. Returns where it starts.
 */
float render_audio_load(MainMenuContext &context, const LoadWindow &window,
                        const char *reading, float graph_width) {
  const ImGuiStyle &style = ImGui::GetStyle();
  const float row = ImGui::GetTextLineHeight();
  const float width =
      graph_width > 0.0f ? graph_width : ImGui::CalcTextSize(reading).x;

  // Never left of the menus: an overlap would take their clicks.
  const float x =
      std::max(ImGui::GetCursorPosX(),
               ImGui::GetWindowWidth() - width - style.FramePadding.x * 2.0f);
  bool clicked = false;
  bool hovered = false;
  if (graph_width > 0.0f) {
    const ImVec2 size(graph_width, row + ui::scale::px(4.0f));
    ImGui::SetCursorPos(ImVec2(x, (ImGui::GetWindowHeight() - size.y) * 0.5f));
    const Panel panel = begin_panel(size, "##audio_load");
    clicked = ImGui::IsItemClicked();
    hovered = ImGui::IsItemHovered();
    draw_load_graph(panel, window);
    panel.draw_list->AddText(
        ImVec2(panel.min.x + style.FramePadding.x,
               panel.min.y + (size.y - row) * 0.5f),
        ImGui::GetColorU32(ImGuiCol_Text), reading);
    end_panel(panel);
  } else {
    // Text in the bar is drawn the frame padding below the cursor.
    ImGui::SetCursorPos(ImVec2(
        x, (ImGui::GetWindowHeight() - row) * 0.5f - style.FramePadding.y));
    ImGui::TextUnformatted(reading);
    clicked = ImGui::IsItemClicked();
    hovered = ImGui::IsItemHovered();
  }

  if (hovered && ImGui::BeginTooltip()) {
    render_load_status(window);
    ImGui::EndTooltip();
  }

  if (clicked) {
    ImGui::OpenPopup("##audio_load_menu");
  }
  if (ui::begin_popup("##audio_load_menu")) {
    render_load_menu(context, window);
    ui::end_popup();
  }
  return x;
}

} // namespace

namespace {

/**
 * The menu holding About and Quit, shown as the app's icon. The label is
 * blank space as wide as the icon, which is drawn over it afterwards so the
 * menu's hover highlight stays underneath.
 */
/// A menu in the bar, highlighted from the bar's top to its bottom.
bool begin_bar_menu(const char *label) {
  ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, ui::bar_item_spacing());
  const bool open = ImGui::BeginMenu(label);
  ImGui::PopStyleVar();
  if (open) {
    ImGui::PushStyleVarY(ImGuiStyleVar_ItemSpacing, ui::list_row_spacing());
  }
  return open;
}

bool begin_app_menu() {
  const ImTextureID icon = app_icon();
  if (icon == ImTextureID{}) {
    return begin_bar_menu("megatoy");
  }
  const float size = ImGui::GetTextLineHeight() + ui::scale::px(10.0f);
  const float space = ImGui::CalcTextSize(" ").x;
  const std::string label =
      std::string(static_cast<std::size_t>(std::ceil(size / space)), ' ') +
      "##megatoy";
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const float label_width = ImGui::CalcTextSize(label.c_str(), nullptr, true).x;
  const bool open = begin_bar_menu(label.c_str());
  // A menu bar item's label starts half an item spacing in.
  const float x = start.x + ImGui::GetStyle().ItemSpacing.x * 0.5f +
                  (label_width - size) * 0.5f;
  const float y = start.y + (ImGui::GetFrameHeight() - size) * 0.5f;
  draw_list->AddImage(icon, ImVec2(x, y),
                      ImVec2(x + size, y + size));
  return open;
}

/// A View menu switch for a panel, its icon in a column of its own so the
/// titles line up.
void panel_menu_item(const char *icon, const char *title, bool &shown) {
  if (ImGui::MenuItemEx(title, icon, nullptr, shown)) {
    shown = !shown;
  }
}

/// How much of the bar fits: the load graph's width (0 leaves it out), and
/// how much of the menus and the middle.
struct BarFit {
  float graph_width = 0.0f;
  BarCentreFit centre = BarCentreFit::Full;
};

/// A menu of the bar, named with its icon, or shown as the icon alone.
struct BarMenu {
  const char *full;
  const char *icon;
  const char *label(BarCentreFit fit) const {
    return fit == BarCentreFit::Icon ? icon : full;
  }
};
constexpr BarMenu kFileMenu{ICON_FA_FILE " File###File",
                            ICON_FA_FILE "###File"};
constexpr BarMenu kEditMenu{ICON_FA_PEN_TO_SQUARE " Edit###Edit",
                            ICON_FA_PEN_TO_SQUARE "###Edit"};
constexpr BarMenu kViewMenu{ICON_FA_EYE " View###View", ICON_FA_EYE "###View"};
constexpr BarMenu kLfoMenu{ICON_FA_WAVE_SQUARE " LFO###LFO",
                           ICON_FA_WAVE_SQUARE "###LFO"};

/// How far a menu in the bar takes the next one along.
float bar_menu_advance(const char *label) {
  return ImGui::CalcTextSize(label, nullptr, true).x +
         ImGui::GetStyle().ItemSpacing.x * 2.0f;
}

/**
 * As the bar narrows, the load graph shrinks to the reading it holds and
 * then goes, then the middle loses its note, and then the menus and the
 * middle keep only their icons.
 * Measured from after the app menu.
 */
BarFit fit_bar(const MainMenuContext &context, const char *reading) {
  const ImGuiStyle &style = ImGui::GetStyle();
  const float spacing = style.ItemSpacing.x;
  const float width = ImGui::GetWindowWidth();
  const float menus_start = ImGui::GetCursorPosX();
  const auto menus_end = [&](BarCentreFit fit) {
    return menus_start + bar_menu_advance(kFileMenu.label(fit)) +
           bar_menu_advance(kEditMenu.label(fit)) +
           bar_menu_advance(kViewMenu.label(fit)) +
           bar_menu_advance(kLfoMenu.label(fit));
  };
  // The room the middle leaves before a load area `load_width` wide: it sits
  // centred in the bar, but never over the menus.
  const auto room = [&](BarCentreFit fit, float load_width) {
    const float load_start = width - load_width - style.FramePadding.x * 2.0f;
    if (!context.bar_centre_size) {
      return load_start - menus_end(fit);
    }
    const BarCentreSize size = context.bar_centre_size(fit);
    const float x =
        std::max(width * 0.5f - size.centred * 0.5f, menus_end(fit) + spacing);
    return load_start - spacing - (x + size.total);
  };

  // The graph holds the reading, so it is never narrower than that.
  const float reading_width = ImGui::CalcTextSize(reading).x;
  const float least = reading_width + style.FramePadding.x * 2.0f;
  const float full = least + ui::scale::px(kLoadGraphWidth);
  const float graph_room = full + room(BarCentreFit::Full, full);
  if (graph_room >= full) {
    return {full, BarCentreFit::Full};
  }
  if (graph_room >= least) {
    return {graph_room, BarCentreFit::Full};
  }
  if (room(BarCentreFit::Full, reading_width) >= 0.0f) {
    return {0.0f, BarCentreFit::Full};
  }
  if (room(BarCentreFit::Short, reading_width) >= 0.0f) {
    return {0.0f, BarCentreFit::Short};
  }
  return {0.0f, BarCentreFit::Icon};
}

void file_menu_items(MainMenuContext &context,
                     const patches::SaveControls &controls) {
  const ImGuiIO &io = ImGui::GetIO();
  auto &session = context.patch_session;
  const bool is_user_patch = controls.saves_in_place;
  const bool save_disabled = !controls.save_enabled;
  const bool mac_behavior = io.ConfigMacOSXBehaviors;
  // The browser keeps Ctrl+N for a new window of its own.
  const char *new_shortcut = megatoy::platform::is_web()
                                 ? nullptr
                                 : (mac_behavior ? "Cmd+N" : "Ctrl+N");
  if (ImGui::MenuItem("New Patch", new_shortcut) && context.new_patch) {
    context.new_patch();
  }
  ImGui::Separator();

  const char *save_shortcut = mac_behavior ? "Cmd+S" : "Ctrl+S";
#if defined(MEGATOY_PLATFORM_WEB)
  // The two ways out of the editor, as the patch tree names them. The
  // shortcuts offer the same two at the pointer, so neither row shows one.
  if (is_user_patch) {
    ImGui::BeginDisabled(save_disabled);
    if (ImGui::MenuItem(controls.save_label, save_shortcut)) {
      trigger_save(session, context.save_state);
    }
    ImGui::EndDisabled();
  }
  if (const auto extension = download_format_menu(session)) {
    download_current_patch(session, *extension);
  }
  if (ImGui::MenuItem("Save to Browser Storage...")) {
    request_save_to_storage(context.save_state);
  }
#else
  const char *save_label = controls.save_label;
  if (save_disabled)
    ImGui::BeginDisabled(true);
  if (ImGui::MenuItem(save_label, save_shortcut)) {
    if (is_user_patch) {
      trigger_save(session, context.save_state);
    } else {
      request_save_as(context.save_state);
    }
  }
  if (save_disabled)
    ImGui::EndDisabled();

  const char *save_as_shortcut =
      mac_behavior ? "Shift+Cmd+S" : "Shift+Ctrl+S";
  if (controls.show_save_as &&
      ImGui::MenuItem("Save As...", save_as_shortcut)) {
    request_save_as(context.save_state);
  }
#endif

  ImGui::Separator();
  // The browser keeps Ctrl+W for closing its own tab.
  const char *close_shortcut = megatoy::platform::is_web()
                                   ? nullptr
                                   : (mac_behavior ? "Cmd+W" : "Ctrl+W");
  if (ImGui::MenuItem("Close Patch", close_shortcut) &&
      context.close_patch) {
    context.close_patch();
  }

  {
    ImGui::Separator();
    // In the browser a folder is copied in rather than referenced, so it
    // is labelled for what it does.
    const char *label = PreferenceManager::folder_add_is_import()
                            ? "Import Folder into Workspace..."
                            : "Add Folder to Workspace...";
    if (ImGui::MenuItem(label)) {
      context.open_add_folder_dialog = true;
    }

    const auto &folders = context.preferences.workspace().folders();
    const bool has_removable_folder = std::any_of(
        folders.begin(), folders.end(), [&](const auto &folder) {
          return !context.preferences.workspace_folder_is_protected(
              folder.path);
        });
    const char *remove_label =
        megatoy::platform::is_web() ? "Delete Folder" : "Remove Folder";
    if (ui::begin_menu(remove_label, has_removable_folder)) {
      std::optional<std::filesystem::path> to_remove;
      for (const auto &folder : folders) {
        if (context.preferences.workspace_folder_is_protected(
                folder.path)) {
          continue;
        }
        if (ImGui::MenuItem(folder.path.string().c_str())) {
          to_remove = folder.path;
        }
      }
      ui::end_menu();
      if (to_remove && context.remove_workspace_folder) {
        context.remove_workspace_folder(*to_remove);
      }
    }
  }
}

void edit_menu_items(MainMenuContext &context) {
  auto &session = context.patch_session;
  auto &history = context.history();
  const ImGuiIO &io = ImGui::GetIO();
  const bool mac_behavior = io.ConfigMacOSXBehaviors;
  const char *undo_shortcut = mac_behavior ? "Cmd+Z" : "Ctrl+Z";
  const char *redo_shortcut = mac_behavior ? "Cmd+Shift+Z" : "Ctrl+Shift+Z";

  std::string undo_label = "Undo";
  if (history.can_undo()) {
    std::string_view change = history.undo_label();
    if (!change.empty()) {
      undo_label.append(" ");
      undo_label.append(change);
    }
  }

  if (ImGui::MenuItem(undo_label.c_str(), undo_shortcut, false,
                      history.can_undo())) {
    if (context.undo) {
      context.undo();
    }
  }

  std::string redo_label = "Redo";
  if (history.can_redo()) {
    std::string_view change = history.redo_label();
    if (!change.empty()) {
      redo_label.append(" ");
      redo_label.append(change);
    }
  }

  if (ImGui::MenuItem(redo_label.c_str(), redo_shortcut, false,
                      history.can_redo())) {
    if (context.redo) {
      context.redo();
    }
  }

  ImGui::Separator();

  // The items name their target the way the undo entries above do, which
  // saves the menu a section heading.
  OperatorCommandContext operator_commands{
      session.current_patch().instrument, context.operator_edit(),
      context.operator_clipboard, context.begin_patch_history,
      context.commit_patch_history};
  render_operator_command_items(operator_commands);
}

void view_menu_items(MainMenuContext &context) {
  auto &ui_prefs = context.ui_prefs;

  if (context.gui.supports_fullscreen()) {
    bool fullscreen = context.gui.is_fullscreen();
    if (ImGui::MenuItem("Fullscreen", nullptr, fullscreen)) {
      context.gui.set_fullscreen(!fullscreen);
    }
    ImGui::Separator();
  }

  panel_menu_item(PATCH_BROWSER_ICON, PATCH_BROWSER_TITLE,
                  ui_prefs.show_patch_selector);
  panel_menu_item(PATCH_LAB_ICON, PATCH_LAB_TITLE, ui_prefs.show_patch_lab);
  panel_menu_item(SOFT_KEYBOARD_ICON, SOFT_KEYBOARD_TITLE,
                  ui_prefs.show_midi_keyboard);
  panel_menu_item(WAVEFORM_ICON, WAVEFORM_TITLE, ui_prefs.show_waveform);
  panel_menu_item(PREFERENCES_ICON, PREFERENCES_TITLE,
                  ui_prefs.show_preferences);

  ImGui::Separator();

  if (ImGui::MenuItem("Reset to Default Layout")) {
    context.gui.reset_layout();
  }
}

/**
 * The LFO's switch and rate. They are how the patch is played rather than
 * the patch, so they mark nothing modified and are not saved with it
 * (ym2612/playback.hpp).
 */
void lfo_menu_items(ym2612::PlaybackSettings &playback) {
  // The chip's eight rates.
  static const char *const kRates[] = {"3.98 Hz", "5.56 Hz", "6.02 Hz",
                                       "6.37 Hz", "6.88 Hz", "9.63 Hz",
                                       "48.1 Hz", "72.2 Hz"};
  ImGui::Checkbox("LFO", &playback.lfo_enable);
  ImGui::BeginDisabled(!playback.lfo_enable);
  int rate = std::clamp<int>(playback.lfo_frequency, 0, 7);
  ImGui::SetNextItemWidth(named_slider_fit_width("Frequency", "48.1 Hz") +
                          ui::scale::px(48.0f));
  if (named_slider_int("Frequency", &rate, 0, 7, kRates[rate])) {
    playback.lfo_frequency = static_cast<uint8_t>(rate);
  }
  ImGui::EndDisabled();
}

} // namespace

void render_main_menu(MainMenuContext &context) {
  bool open_about = false;
  const ImGuiIO &io = ImGui::GetIO();
  // A taller bar than the frame padding alone would give. It stays pushed
  // until the bar ends so the items centre in it and the menus open below it.
  const ImVec2 padding = ImGui::GetStyle().FramePadding;
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                      ImVec2(padding.x, padding.y + ui::scale::px(8.0f)));
  if (ImGui::BeginMainMenuBar()) {
    if (begin_app_menu()) {
      const bool mac_behavior = io.ConfigMacOSXBehaviors;

      if (ImGui::MenuItem("About megatoy")) {
        open_about = true;
      }
      if (ImGui::MenuItem("Change Log...")) {
        open_changelog_dialog();
      }
      if (context.gui.supports_quit()) {
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", mac_behavior ? "Cmd+Q" : "Alt+F4")) {
          context.gui.set_should_close(true);
        }
      }
      ui::end_menu();
    }
    auto &session = context.patch_session;
    const auto controls =
        patches::save_controls(session, session.active_document());
    const bool is_user_patch = controls.saves_in_place;
    const bool save_disabled = !controls.save_enabled;
    const LoadWindow load = load_window(context.audio_load.history(),
                                        ui::scale::px(kLoadGraphWidth));
    char reading[32];
    format_load_reading(
        reading, sizeof(reading), load,
        audio::load_reading_from_int(context.ui_prefs.audio_load_reading));
    const BarFit fit = fit_bar(context, reading);
    if (begin_bar_menu(kFileMenu.label(fit.centre))) {
      file_menu_items(context, controls);
      ui::end_menu();
    }
    if (begin_bar_menu(kEditMenu.label(fit.centre))) {
      edit_menu_items(context);
      ui::end_menu();
    }
    if (begin_bar_menu(kViewMenu.label(fit.centre))) {
      view_menu_items(context);
      ui::end_menu();
    }
    if (begin_bar_menu(kLfoMenu.label(fit.centre))) {
      lfo_menu_items(context.patch_session.playback());
      ui::end_menu();
    }

    // The folder picker is modal, so it is opened after the menu closes.
    if (context.open_add_folder_dialog) {
      context.open_add_folder_dialog = false;
      auto sync = context.sync_workspace;
      context.preferences.request_add_workspace_folder(
          [sync]() {
            if (sync) {
              sync();
            }
          },
          context.confirm_replace_folders);
    }

    // Not gated on supports_quit(): saving has nothing to do with whether
    // the platform can be quit, and on the web that gate never opens.
    if (!save_disabled) {
      const bool primary_modifier = (io.KeyCtrl || io.KeySuper) && !io.KeyShift;
      if (primary_modifier && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (is_user_patch) {
          trigger_save(session, context.save_state);
        } else {
          request_save_as(context.save_state);
        }
      }
    }
    const bool save_as_modifier = (io.KeyCtrl || io.KeySuper) && io.KeyShift;
    if (save_as_modifier && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
      request_save_as(context.save_state);
    }

    const float menus_end = ImGui::GetCursorPosX();
    const float row_y = ImGui::GetCursorPosY();
    const float load_start =
        render_audio_load(context, load, reading, fit.graph_width);
    if (context.bar_centre) {
      context.bar_centre(fit.centre, menus_end, load_start, row_y);
    }

    ImGui::EndMainMenuBar();
  }
  ImGui::PopStyleVar();

  if (open_about) {
    open_about_dialog();
  }
  render_about_dialog();
  render_changelog_dialog();
}

} // namespace ui
