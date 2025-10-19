#include "app_state.hpp"
#include "core/types.hpp"
#include "gui/ui_renderer.hpp"
#include "midi/midi_input_manager.hpp"
#include "ym2612/channel.hpp"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <imgui.h>
#include <iostream>

namespace {

void handle_file_drop(GLFWwindow *window, int count, const char **paths) {
  if (paths == nullptr || count <= 0) {
    return;
  }

  auto *state = static_cast<AppState *>(glfwGetWindowUserPointer(window));
  if (state == nullptr) {
    return;
  }

  for (int i = 0; i < count; ++i) {
    if (paths[i] != nullptr) {
      state->handle_patch_file_drop(std::filesystem::path(paths[i]));
    }
  }
}

} // namespace

int main(int argc, char *argv[]) {
  // Initialize application state
  AppState app_state;
  app_state.init();

  // Setup window callbacks
  if (GLFWwindow *window = app_state.gui().get_window()) {
    glfwSetWindowUserPointer(window, &app_state);
    glfwSetDropCallback(window, handle_file_drop);
  }

  // Initialize MIDI
  MidiInputManager midi;
  midi.init();

  // Create UI renderer (UI rendering only)
  ui::UIRenderer ui_renderer(app_state);

  std::cout << "Application initialized. Use the button in the window to play "
               "C4 note.\n";

  // Main application loop
  while (!app_state.gui().should_close()) {
    // Poll events
    app_state.gui().poll_events();

    // Update MIDI
    midi.poll();
    midi.dispatch(app_state);

    // Begin frame
    app_state.gui().begin_frame();

    // Handle history shortcuts
    app_state.history().handle_shortcuts(app_state);

    // Render UI
    ui_renderer.render();

    // End frame
    app_state.gui().end_frame();
  }

  // Shutdown
  app_state.shutdown();
  std::cout << "Goodbye!\n";
  return 0;
}
