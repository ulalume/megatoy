#include "app_state.hpp"
#include "midi_usb.hpp"
#include "types.hpp"
#include "ui/keyboard_typing.hpp"
#include "ui/main_menu.hpp"
#include "ui/midi_keyboard.hpp"
#include "ui/mml_console.hpp"
#include "ui/patch_editor.hpp"
#include "ui/patch_selector.hpp"
#include "ui/preferences.hpp"
#include "ym2612/channel.hpp"
#include <imgui.h>
#include <iostream>

namespace {

void handle_history_shortcuts(AppState &app_state) {
  ImGuiIO &io = ImGui::GetIO();

  if (io.WantTextInput || app_state.input_state().text_input_focused) {
    return;
  }

  const bool primary_modifier = io.KeyCtrl || io.KeySuper;
  if (!primary_modifier) {
    return;
  }

  const bool shift = io.KeyShift;
  auto &history = app_state.history();

  if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
    if (shift) {
      if (history.can_redo()) {
        history.redo(app_state);
      }
    } else if (history.can_undo()) {
      history.undo(app_state);
    }
  } else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
    if (history.can_redo()) {
      history.redo(app_state);
    }
  }
}

} // namespace

int main(int argc, char *argv[]) {
  std::cout << "VGM Real-time Audio Test with Dear ImGui\n";

  AppState app_state;
  app_state.init();

  // Set up frequency for C4 note
  app_state.device()
      .channel(ym2612::ChannelIndex::Fm1)
      .write_frequency({4, Key::C});

  // Init MIDI
  MidiInputManager midi;
  midi.init();

  std::cout
      << "GUI initialized. Use the button in the window to play C4 note.\n";

  // Main GUI loop
  while (!app_state.gui_manager().should_close()) {
    // Poll events
    app_state.gui_manager().poll_events();
    // Start new frame
    app_state.gui_manager().begin_frame();

    // Midi USB update
    midi.poll(app_state);

    // ImGui::ShowDemoWindow();

    ui::render_main_menu(app_state);
    handle_history_shortcuts(app_state);
    // Render UI panels
    ui::render_patch_editor(app_state);

    ui::render_midi_keyboard(app_state);
    ui::render_preferences_window(app_state);
    ui::render_patch_selector(app_state);

    ui::render_keyboard_typing(app_state);
    ui::render_mml_console(app_state);

    app_state.input_state().text_input_focused = false;

    app_state.preference_manager().set_ui_preferences(
        app_state.ui_state().prefs);

    // End frame and render
    app_state.gui_manager().end_frame();
  }

  app_state.shutdown();
  std::cout << "Goodbye!\n";
  return 0;
}
