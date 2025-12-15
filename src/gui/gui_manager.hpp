#pragma once

#include "gui/styles/theme.hpp"
#include "preferences/preference_manager.hpp"
#include <string>

#include <SDL3/SDL.h>

/**
 * GuiManager - Unified GUI system management
 *
 * Consolidates GuiSubsystem, GuiRuntime, and GuiManager into a single
 * class that handles all GUI-related functionality including:
 * - SDL3 window management and initialization
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
   * Get SDL window pointer
   */
  SDL_Window *get_window() const { return window_; }

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

  bool supports_fullscreen() const;
  bool supports_quit() const;
  bool supports_waveform() const;
  bool supports_patch_history() const;

  /**
   * Theme management
   */
  void set_theme(ui::styles::ThemeId theme);
  ui::styles::ThemeId theme() const { return theme_; }

  /**
   * File drop callback management
   */
  void set_drop_callback(void *user_pointer,
                         void (*callback)(void *user_pointer, int count,
                                          const char **paths));

  // Web-specific ini bridge (no-op on native).
  void sync_web_imgui_ini();
  void save_web_imgui_ini_if_needed();

private:
  // Core GUI system
  PreferenceManager &preferences_;
  SDL_Window *window_;
  SDL_GLContext gl_context_;
  bool initialized_;
  bool should_close_;
  SDL_WindowID window_id_;

  // Window state
  bool fullscreen_;
  int windowed_pos_x_;
  int windowed_pos_y_;
  int windowed_width_;
  int windowed_height_;

  // ImGui state
  bool first_frame_;
  bool first_end_frame_;
  bool pending_imgui_ini_update_;
  std::string imgui_ini_file_path_;
  ui::styles::ThemeId theme_;

  // Internal methods
  void set_imgui_ini_file(const std::string &path);
  void apply_imgui_ini_binding();
  void dispatch_drop_event(const char *path);
  void setup_default_layout(unsigned int dockspace_id);

  // Drop callback state
  void *drop_user_pointer_;
  void (*drop_callback_)(void *user_pointer, int count, const char **paths);

  bool web_ini_loaded_ = false;
};
