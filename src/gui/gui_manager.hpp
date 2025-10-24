#pragma once

#include "gui/styles/theme.hpp"
#include "preferences/preference_manager.hpp"

#include <GLFW/glfw3.h>
#include <string>

/**
 * GuiManager - Unified GUI system management
 *
 * Consolidates GuiSubsystem, GuiRuntime, and GuiManager into a single
 * class that handles all GUI-related functionality including:
 * - GLFW window management and initialization
 * - ImGui setup and rendering
 * - Theme management and preferences integration
 * - File dialog integration
 */
class GuiManager {
public:
  explicit GuiManager(PreferenceManager &preferences);
  ~GuiManager();

  // Non-copyable, non-movable
  GuiManager(const GuiManager &) = delete;
  GuiManager &operator=(const GuiManager &) = delete;
  GuiManager(GuiManager &&) = delete;
  GuiManager &operator=(GuiManager &&) = delete;

  /**
   * Initialize and start the complete GUI system
   * @param window_title Window title
   * @param width Initial window width
   * @param height Initial window height
   * @return true on success, false on failure
   */
  bool initialize(const std::string &window_title, int width = 800,
                  int height = 600);

  /**
   * Shutdown and cleanup complete GUI system
   */
  void shutdown();

  /**
   * Check if window should close
   */
  bool get_should_close() const;

  /**
   * Set the should close flag
   */
  void set_should_close(bool value);

  /**
   * Begin new frame
   */
  void begin_frame();

  /**
   * End frame and render
   */
  void end_frame();

  /**
   * Poll events
   */
  void poll_events();

  /**
   * Get GLFW window pointer
   */
  GLFWwindow *get_window() const { return window_; }

  /**
   * Sync ImGui ini file with preferences
   */
  void sync_imgui_ini();

  /**
   * Apply theme from preferences
   */
  void apply_theme();

  /**
   * Reset ImGui layout
   */
  void reset_layout();

  /**
   * Fullscreen management
   */
  bool is_fullscreen() const { return fullscreen_; }
  void set_fullscreen(bool enable);
  void toggle_fullscreen();

  /**
   * Theme management
   */
  void set_theme(ui::styles::ThemeId theme);
  ui::styles::ThemeId theme() const { return theme_; }

private:
  // Core GUI system
  PreferenceManager &preferences_;
  GLFWwindow *window_;
  bool initialized_;

  // Window state
  bool fullscreen_;
  int windowed_pos_x_;
  int windowed_pos_y_;
  int windowed_width_;
  int windowed_height_;

  // ImGui state
  bool first_frame_;
  bool pending_imgui_ini_update_;
  std::string imgui_ini_file_path_;
  ui::styles::ThemeId theme_;

  // Internal methods
  void set_imgui_ini_file(const std::string &path);
  void apply_imgui_ini_binding();

  // Static callbacks
  static void glfw_error_callback(int error, const char *description);
};
