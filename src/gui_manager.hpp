#pragma once

#include <GLFW/glfw3.h>
#include <string>

class GuiManager {
public:
  GuiManager();
  ~GuiManager();

  // Initialize the GUI system
  bool init(const std::string &window_title = "VGM Audio Test", int width = 800,
            int height = 600);

  // Cleanup
  void shutdown();

  // Check if window should close
  bool should_close() const;

  // Begin new frame
  void begin_frame();

  // End frame and render
  void end_frame();

  // Get GLFW window pointer
  GLFWwindow *get_window() const { return window; }

  // Poll events
  void poll_events();

  bool is_fullscreen() const { return fullscreen; }
  void set_fullscreen(bool enable);
  void toggle_fullscreen();

private:
  GLFWwindow *window;
  bool initialized;
  bool fullscreen;
  int windowed_pos_x;
  int windowed_pos_y;
  int windowed_width;
  int windowed_height;

  // Static callbacks
  static void glfw_error_callback(int error, const char *description);
};
