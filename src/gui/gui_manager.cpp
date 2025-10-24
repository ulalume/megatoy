#include "gui_manager.hpp"
#include "gui/components/preview/algorithm_preview.hpp"
#include "gui/components/preview/ssg_preview.hpp"
#include "gui/styles/theme.hpp"
#include "gui/window_title.hpp"
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <iostream>

#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers

#if defined(_MSC_VER) && (_MSC_VER >= 1900) &&                                 \
    !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

GuiManager::GuiManager(PreferenceManager &preferences)
    : preferences_(preferences), window_(nullptr), initialized_(false),
      fullscreen_(false), windowed_pos_x_(0), windowed_pos_y_(0),
      windowed_width_(0), windowed_height_(0), first_frame_(true),
      pending_imgui_ini_update_(false), imgui_ini_file_path_(),
      theme_(ui::styles::ThemeId::MegatoyDark) {}

GuiManager::~GuiManager() { shutdown(); }

void GuiManager::glfw_error_callback(int error, const char *description) {
  std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

bool GuiManager::initialize(const std::string &window_title, int width,
                            int height) {
  if (initialized_) {
    return true;
  }

  // Setup GLFW
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW" << std::endl;
    return false;
  }

  // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
  // GL ES 2.0 + GLSL 100
  const char *glsl_version = "#version 100";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
  // GL 3.2 + GLSL 150
  const char *glsl_version = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // 3.2+ only
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Required on Mac
#else
  // GL 3.0 + GLSL 130
  const char *glsl_version = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

  // Create window with graphics context
  window_ =
      glfwCreateWindow(width, height, window_title.c_str(), nullptr, nullptr);
  if (window_ == nullptr) {
    std::cerr << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return false;
  }

  glfwMakeContextCurrent(window_);
  glfwSwapInterval(1); // Enable vsync

  glfwGetWindowPos(window_, &windowed_pos_x_, &windowed_pos_y_);
  glfwGetWindowSize(window_, &windowed_width_, &windowed_height_);

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;

  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls

  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking

  // Setup Dear ImGui style
  ImGuiStyle &style = ImGui::GetStyle();
  style.ScrollbarSize = 8;
  style.ScrollbarRounding = 0;
  style.ScrollbarPadding = 0;
  style.SeparatorTextBorderSize = 1;
  style.FramePadding = ImVec2(4, 2);

  // Apply theme from preferences
  set_theme(preferences_.theme());

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOpenGL(window_, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  // Initialize file dialog
  if (!preferences_.initialize_file_dialog()) {
    std::cerr << "Native File Dialog unavailable; directory picker disabled\n";
  }

  // Sync ImGui ini file
  sync_imgui_ini();

  initialized_ = true;
  return true;
}

void GuiManager::shutdown() {
  if (!initialized_) {
    return;
  }

  // Reset preview textures
  ui::reset_algorithm_preview_textures();
  ui::reset_ssg_preview_textures();

  // Cleanup
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  if (window_) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
  glfwTerminate();

  fullscreen_ = false;
  initialized_ = false;
}

bool GuiManager::get_should_close() const {
  return window_ ? glfwWindowShouldClose(window_) : true;
}

void GuiManager::set_should_close(bool value) {
  if (window_) {
    glfwSetWindowShouldClose(window_, value ? GLFW_TRUE : GLFW_FALSE);
  }
}

void GuiManager::begin_frame() {
  if (!initialized_) {
    return;
  }

  // Start the Dear ImGui frame
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGuiID dockspace_id = viewport->ID;

  ImGui::DockSpaceOverViewport(dockspace_id, viewport);

  if (first_frame_) {
    first_frame_ = false;

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

    ImGuiID dock_main_id = dockspace_id;
    ImGuiID dock_id_left = ImGui::DockBuilderSplitNode(
        dock_main_id, ImGuiDir_Left, 0.3f, nullptr, &dock_main_id);

    ImGuiID dock_id_left_down;
    dock_id_left_down = ImGui::DockBuilderSplitNode(
        dock_id_left, ImGuiDir_Down, 0.25f, nullptr, &dock_id_left);

    ImGuiID dock_id_right_down;
    dock_id_right_down = ImGui::DockBuilderSplitNode(
        dock_main_id, ImGuiDir_Down, 0.20f, nullptr, &dock_main_id);

    ImGui::DockBuilderDockWindow(ui::PATCH_BROWSER_TITLE, dock_id_left);
    auto patch_editor_title = std::string(ui::PATCH_EDITOR_TITLE) + "###" +
                              std::string(ui::PATCH_EDITOR_TITLE);
    ImGui::DockBuilderDockWindow(patch_editor_title.c_str(), dock_main_id);
    ImGui::DockBuilderDockWindow(ui::SOFT_KEYBOARD_TITLE, dock_id_right_down);
    ImGui::DockBuilderDockWindow(ui::MML_CONSOLE_TITLE, dock_id_right_down);
    ImGui::DockBuilderDockWindow(ui::WAVEFORM_TITLE, dock_id_left_down);

    // Finish the dockspace
    ImGui::DockBuilderFinish(dockspace_id);
  }
}

void GuiManager::end_frame() {
  if (!initialized_) {
    return;
  }

  // Rendering
  ImGui::Render();
  int display_w, display_h;
  glfwGetFramebufferSize(window_, &display_w, &display_h);
  glViewport(0, 0, display_w, display_h);
  glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  glfwSwapBuffers(window_);
}

void GuiManager::poll_events() {
  if (initialized_) {
    glfwPollEvents();
  }
}

void GuiManager::sync_imgui_ini() {
  const auto ini_path = preferences_.get_imgui_ini_file();
  set_imgui_ini_file(ini_path.generic_string());
}

void GuiManager::apply_theme() {
  set_theme(preferences_.theme());
  preferences_.set_theme(theme_);
}

void GuiManager::reset_layout() { first_frame_ = true; }

void GuiManager::set_fullscreen(bool enable) {
  if (!initialized_ || !window_) {
    return;
  }

  if (enable == fullscreen_) {
    return;
  }

  if (enable) {
    GLFWmonitor *monitor = glfwGetWindowMonitor(window_);
    if (monitor == nullptr) {
      monitor = glfwGetPrimaryMonitor();
    }
    if (monitor == nullptr) {
      return;
    }

    glfwGetWindowPos(window_, &windowed_pos_x_, &windowed_pos_y_);
    glfwGetWindowSize(window_, &windowed_width_, &windowed_height_);

    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    if (mode == nullptr) {
      return;
    }

    glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height,
                         mode->refreshRate);
    fullscreen_ = true;
  } else {
    glfwSetWindowMonitor(window_, nullptr, windowed_pos_x_, windowed_pos_y_,
                         windowed_width_ > 0 ? windowed_width_ : 800,
                         windowed_height_ > 0 ? windowed_height_ : 600, 0);
    fullscreen_ = false;
  }
}

void GuiManager::toggle_fullscreen() { set_fullscreen(!fullscreen_); }

void GuiManager::set_theme(ui::styles::ThemeId theme) {
  theme_ = theme;
  ui::styles::apply_theme(theme_);
  if (ImGui::GetCurrentContext() != nullptr) {
    ui::reset_algorithm_preview_textures();
    ui::reset_ssg_preview_textures();
  }
}

void GuiManager::set_imgui_ini_file(const std::string &path) {
  imgui_ini_file_path_ = path;
  if (imgui_ini_file_path_.empty()) {
    first_frame_ = true;
  } else {
    first_frame_ = !std::filesystem::exists(imgui_ini_file_path_);
  }
  pending_imgui_ini_update_ = true;
  apply_imgui_ini_binding();
}

void GuiManager::apply_imgui_ini_binding() {
  if (!pending_imgui_ini_update_) {
    return;
  }

  if (ImGui::GetCurrentContext() == nullptr) {
    return;
  }

  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename =
      imgui_ini_file_path_.empty() ? nullptr : imgui_ini_file_path_.c_str();
  pending_imgui_ini_update_ = false;
}
