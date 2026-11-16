#pragma once

#include "gui/styles/theme.hpp"
#include "gui/ui_scale.hpp"
#include "preferences/preference_manager.hpp"
#include <string>

#include <SDL3/SDL.h>

/**
 * The GUI system: the SDL3 window and its events, ImGui setup and rendering,
 * the theme and scale from the preferences, and the file dialog's lifetime.
 */
class GuiManager {
public:
  explicit GuiManager(PreferenceManager &preferences);
  ~GuiManager();

  GuiManager(const GuiManager &) = delete;
  GuiManager &operator=(const GuiManager &) = delete;
  GuiManager(GuiManager &&) = delete;
  GuiManager &operator=(GuiManager &&) = delete;

  /// Initialize and start the complete GUI system; false on failure.
  bool initialize(const std::string &window_title, int width = 800,
                  int height = 600);

  void shutdown();

  bool get_should_close() const;

  void set_should_close(bool value);

  /// Begin a new frame and apply any requested layout reset; the footer is
  /// fitted to `waveform_shown`, the waveform's preference.
  void begin_frame(bool waveform_shown);

  void end_frame();

  /**
   * Wait up to `timeout_ms` for an event, then drain the SDL queue.
   * Returns immediately when input arrives so an idle frame cap adds no
   * interaction latency.
   */
  bool poll_events(int timeout_ms = 0);

  bool is_minimized() const;

  /**
   * Sync ImGui ini file with preferences
   */
  void sync_imgui_ini();

  /**
   * Apply theme from preferences
   */
  void apply_theme();

  /// Rebuilds the default arrangement from the next frame.
  void reset_layout();

  /// The title of the application's window; unchanged titles cost nothing.
  void set_window_title(const std::string &title);

  bool is_fullscreen() const { return fullscreen_; }
  void set_fullscreen(bool enable);

  bool supports_fullscreen() const;
  bool supports_quit() const;

  void set_theme(ui::styles::ThemeId theme);
  ui::styles::ThemeId theme() const { return theme_; }

  /// Rebuild the style for a scale preference; 0 follows the display.
  void apply_ui_scale(float preference);
  /// Enlargement the display asks content for; 1.0 when it cannot be read.
  float display_scale() const;

  // Web-specific ini bridge (no-op on native).
  void sync_web_imgui_ini();

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
  std::string window_title_;
  ui::styles::ThemeId theme_;
  float ui_scale_preference_ = ui::scale::kAuto;
  float ui_scale_ = 1.0f;
  bool web_layout_loaded_ = false;
  /// The arrangement just loaded is still to be checked against the areas.
  bool check_loaded_layout_ = true;

  void set_imgui_ini_file(const std::string &path);
  void apply_imgui_ini_binding();
  void process_event(const SDL_Event &event);
  void setup_default_layout(unsigned int dockspace_id);

  bool web_ini_loaded_ = false;
};
