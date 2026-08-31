#include "main_menu.hpp"
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
#include "gui/save_export_actions.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "gui/window_title.hpp"
#include "ym2612/chip.hpp"
#include <cstddef>
#include <cstdio>
#include <imgui.h>
#include <iterator>
#include <string>
#include <string_view>

namespace ui {

namespace {

// Full scale on the graph is the render deadline: 1.0 means a block took as
// long to produce as it takes to play.
constexpr float kLoadWarningLevel = 0.7f;
constexpr float kLoadGraphWidth = 64.0f;
// How far back the graph reaches: long enough that a glitch is still on
// screen once the ear has noticed it and the eye has arrived.
constexpr float kLoadWindowMs = 10000.0f;
constexpr std::size_t kMaxLoadColumns = 256;

// The drawn window, folded to one column per pixel by taking the highest
// value in each: the reading beside the graph is the top of what is drawn.
struct LoadWindow {
  std::array<float, kMaxLoadColumns> columns{};
  std::size_t count = 0;
  float peak = 0.0f;
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
  for (std::size_t i = 0; i < used; ++i) {
    const std::size_t column = out.count < 2 ? 0
                                             : i * (out.count - 1) / (used - 1);
    const float value = history.values[first + i];
    out.columns[column] = std::max(out.columns[column], value);
    out.peak = std::max(out.peak, value);
  }
  return out;
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
void render_load_menu(MainMenuContext &context) {
  auto &ui_prefs = context.ui_prefs;

  ImGui::SeparatorText("Improve performance");

  const bool core_at_best =
      ui_prefs.ym2612_core == static_cast<int>(ym2612::CoreType::Ymfm);
  if (ImGui::MenuItem(core_at_best ? "Switch core to ymfm (already set)"
                                   : "Switch core to ymfm...",
                      nullptr, false, !core_at_best)) {
    context.open_sound_preferences = true;
  }

  const int largest_buffer =
      kAudioBufferChoices[std::size(kAudioBufferChoices) - 1];
  const int buffer_frames = ui_prefs.audio_buffer_frames > 0
                                ? ui_prefs.audio_buffer_frames
                                : context.default_audio_buffer_frames;
  const bool buffer_at_best = buffer_frames >= largest_buffer;
  if (ImGui::MenuItem(buffer_at_best ? "Increase buffer size (already set)"
                                     : "Increase buffer size...",
                      nullptr, false, !buffer_at_best)) {
    context.open_sound_preferences = true;
  }

#if defined(MEGATOY_PLATFORM_WEB)
  // The browser draws the interface on the thread that renders the audio, so
  // the time the waveform takes comes out of the same deadline. Where audio
  // has a thread of its own it cannot, and the item is not offered.
  const bool waveform_at_best = !ui_prefs.show_waveform;
  if (ImGui::MenuItem(waveform_at_best ? "Hide waveform (already set)"
                                       : "Hide waveform",
                      nullptr, false, !waveform_at_best)) {
    ui_prefs.show_waveform = false;
  }
#endif
}

/// The load graph, at the right-hand end of the bar.
void render_audio_load(MainMenuContext &context) {
  const ImGuiStyle &style = ImGui::GetStyle();
  const ImVec2 size(ui::scale::px(kLoadGraphWidth), ImGui::GetTextLineHeight());

  const auto history = context.audio_load.history();
  const LoadWindow window = load_window(history, size.x);
  char reading[8];
  std::snprintf(reading, sizeof(reading), "%.0f%%",
                static_cast<double>(window.peak) * 100.0);
  const float reading_width = ImGui::CalcTextSize(reading).x;
  const float row = ImGui::GetTextLineHeight();

  // Never left of the menus: an overlap would take their clicks.
  const float x = std::max(ImGui::GetCursorPosX(),
                           ImGui::GetWindowWidth() - size.x - reading_width -
                               style.ItemSpacing.x -
                               style.FramePadding.x * 2.0f);
  const float y = (ImGui::GetWindowHeight() - row) * 0.5f;
  ImGui::SetCursorPos(ImVec2(x, y));
  ImGui::TextUnformatted(reading);

  ImGui::SetCursorPos(
      ImVec2(x + reading_width + style.ItemSpacing.x, y));
  const Panel panel = begin_panel(size, "##audio_load");
  const bool clicked = ImGui::IsItemClicked();
  const bool hovered = ImGui::IsItemHovered();
  draw_load_graph(panel, window);
  end_panel(panel);

  if (hovered) {
    ImGui::SetTooltip("Audio load");
  }

  if (clicked) {
    ImGui::OpenPopup("##audio_load_menu");
  }
  if (ImGui::BeginPopup("##audio_load_menu")) {
    render_load_menu(context);
    ImGui::EndPopup();
  }
}

} // namespace

void render_main_menu(MainMenuContext &context) {
  bool open_about = false;
  const ImGuiIO &io = ImGui::GetIO();
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("megatoy")) {
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
      ImGui::EndMenu();
    }
    auto &session = context.patch_session;
    const bool is_patch_modified = session.is_modified();
    const bool is_user_patch = session.current_patch_is_user_patch();
    const bool save_disabled = is_user_patch && !is_patch_modified;
    if (ImGui::BeginMenu("File")) {
      const bool mac_behavior = io.ConfigMacOSXBehaviors;
      const char *save_shortcut = mac_behavior ? "Cmd+S" : "Ctrl+S";
      const char *save_label = save_label_for(session, is_user_patch);
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
      if (is_user_patch && ImGui::MenuItem("Save As...", save_as_shortcut)) {
        request_save_as(context.save_state);
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
        if (ImGui::BeginMenu(remove_label, has_removable_folder)) {
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
          ImGui::EndMenu();
          if (to_remove && context.remove_workspace_folder) {
            context.remove_workspace_folder(*to_remove);
          }
        }
      }

      ImGui::EndMenu();
    }

    // The folder picker is modal, so it is opened after the menu closes.
    if (context.open_add_folder_dialog) {
      context.open_add_folder_dialog = false;
      auto sync = context.sync_workspace;
      context.preferences.request_add_workspace_folder([sync]() {
        if (sync) {
          sync();
        }
      });
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

    if (ImGui::BeginMenu("Edit")) {
      auto &history = context.history;
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
          session.current_patch().instrument, context.operator_edit,
          context.begin_patch_history, context.commit_patch_history};
      render_operator_command_items(operator_commands);

      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
      auto &ui_prefs = context.ui_prefs;

      if (context.gui.supports_fullscreen()) {
        bool fullscreen = context.gui.is_fullscreen();
        if (ImGui::MenuItem("Fullscreen", nullptr, fullscreen)) {
          context.gui.set_fullscreen(!fullscreen);
        }
        ImGui::Separator();
      }

      ImGui::MenuItem(PATCH_BROWSER_TITLE, nullptr,
                      &ui_prefs.show_patch_selector);
      ImGui::MenuItem(PATCH_EDITOR_TITLE, nullptr, &ui_prefs.show_patch_editor);
      ImGui::MenuItem(PATCH_LAB_TITLE, nullptr, &ui_prefs.show_patch_lab);
      ImGui::MenuItem(SOFT_KEYBOARD_TITLE, nullptr,
                      &ui_prefs.show_midi_keyboard);
      ImGui::MenuItem(MML_CONSOLE_TITLE, nullptr, &ui_prefs.show_mml_console);
      ImGui::MenuItem(WAVEFORM_TITLE, nullptr, &ui_prefs.show_waveform);
      ImGui::MenuItem(PREFERENCES_TITLE, nullptr, &ui_prefs.show_preferences);

      ImGui::Separator();

      // Reset buttons
      if (ImGui::MenuItem("Reset to Default View")) {
        context.preferences.reset_ui_preferences();
        context.ui_prefs = context.preferences.ui_preferences();
        context.open_add_folder_dialog = false;
        context.gui.reset_layout();
        context.gui.set_theme(ui::styles::ThemeId::MegatoyDark);
      }
      ImGui::EndMenu();
    }

    render_audio_load(context);

    ImGui::EndMainMenuBar();
  }

  if (open_about) {
    open_about_dialog();
  }
  render_about_dialog();
  render_changelog_dialog();
}

} // namespace ui
