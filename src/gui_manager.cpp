#include "gui_manager.hpp"
#include "ui/preview/algorithm_preview.hpp"
#include "ui/preview/ssg_preview.hpp"
#include "ui/styles/theme.hpp"
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

GuiManager::GuiManager()
    : window(nullptr), initialized(false), fullscreen(false), windowed_pos_x(0),
      windowed_pos_y(0), windowed_width(0), windowed_height(0) {}

GuiManager::~GuiManager() { shutdown(); }

void GuiManager::glfw_error_callback(int error, const char *description) {
  std::cerr << "GLFW Error " << error << ": " << description << std::endl;
}

bool GuiManager::init(const std::string &window_title, int width, int height) {
  if (initialized) {
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
  window =
      glfwCreateWindow(width, height, window_title.c_str(), nullptr, nullptr);
  if (window == nullptr) {
    std::cerr << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return false;
  }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1); // Enable vsync

  glfwGetWindowPos(window, &windowed_pos_x, &windowed_pos_y);
  glfwGetWindowSize(window, &windowed_width, &windowed_height);

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
  set_theme(theme_);

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  initialized = true;
  return true;
}

static char *copy_string(const char s[], int size) {
  char *s2;
  s2 = (char *)malloc(((size + 1) * sizeof(char)));

  if (s2)
    strcpy(s2, s);
  return (char *)s2;
}

void GuiManager::set_imgui_ini_file(const std::string &path) {
  first_frame = !std::filesystem::exists(path);
  ImGui::GetIO().IniFilename = copy_string(path.c_str(), path.length());
}

void GuiManager::reset_layout() { first_frame = true; }

void GuiManager::set_theme(ui::styles::ThemeId theme) {
  theme_ = theme;
  ui::styles::apply_theme(theme_);
  if (ImGui::GetCurrentContext() != nullptr) {
    ui::reset_algorithm_preview_textures();
    ui::reset_ssg_preview_textures();
  }
}

void GuiManager::shutdown() {
  if (!initialized) {
    return;
  }

  // Cleanup
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  if (window) {
    glfwDestroyWindow(window);
    window = nullptr;
  }
  glfwTerminate();

  fullscreen = false;
  initialized = false;
}

bool GuiManager::should_close() const {
  return window ? glfwWindowShouldClose(window) : true;
}

void GuiManager::begin_frame() {
  if (!initialized) {
    return;
  }

  // Start the Dear ImGui frame
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGuiID dockspace_id = viewport->ID;

  ImGui::DockSpaceOverViewport(dockspace_id, viewport);

  if (first_frame) {
    first_frame = false;

    std::cout << "First frame" << std::endl;

    // default layout
    ImGui::DockBuilderRemoveNode(dockspace_id); // clear
    ImGui::DockBuilderAddNode(dockspace_id,
                              ImGuiDockNodeFlags_DockSpace); // new DockSpace
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

    ImGuiID dock_main_id = dockspace_id;
    ImGuiID dock_id_left = ImGui::DockBuilderSplitNode(
        dock_main_id, ImGuiDir_Left, 0.3f, nullptr, &dock_main_id);

    ImGuiID dock_id_left_down;
    dock_id_left_down = ImGui::DockBuilderSplitNode(
        dock_id_left, ImGuiDir_Down, 0.36f, nullptr, &dock_id_left);

    ImGuiID dock_id_left_down_down;
    dock_id_left_down_down = ImGui::DockBuilderSplitNode(
        dock_id_left_down, ImGuiDir_Down, 0.45f, nullptr, &dock_id_left_down);

    ImGuiID dock_id_right_down;
    dock_id_right_down = ImGui::DockBuilderSplitNode(
        dock_main_id, ImGuiDir_Down, 0.2f, nullptr, &dock_main_id);

    ImGui::DockBuilderDockWindow("Patch Browser", dock_id_left);
    ImGui::DockBuilderDockWindow("Keyboard Typing", dock_id_left_down);
    ImGui::DockBuilderDockWindow("Patch Editor", dock_main_id);
    ImGui::DockBuilderDockWindow("Soft Keyboard", dock_id_right_down);
    ImGui::DockBuilderDockWindow("MML Console", dock_id_right_down);
    ImGui::DockBuilderDockWindow("Waveform", dock_id_left_down_down);

    // Finish the dockspace
    ImGui::DockBuilderFinish(dockspace_id);
  }
}

void GuiManager::end_frame() {
  if (!initialized) {
    return;
  }

  // Rendering
  ImGui::Render();
  int display_w, display_h;
  glfwGetFramebufferSize(window, &display_w, &display_h);
  glViewport(0, 0, display_w, display_h);
  glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  glfwSwapBuffers(window);
}

void GuiManager::poll_events() {
  if (initialized) {
    glfwPollEvents();
  }
}

void GuiManager::set_fullscreen(bool enable) {
  if (!initialized || !window) {
    return;
  }

  if (enable == fullscreen) {
    return;
  }

  if (enable) {
    GLFWmonitor *monitor = glfwGetWindowMonitor(window);
    if (monitor == nullptr) {
      monitor = glfwGetPrimaryMonitor();
    }
    if (monitor == nullptr) {
      return;
    }

    glfwGetWindowPos(window, &windowed_pos_x, &windowed_pos_y);
    glfwGetWindowSize(window, &windowed_width, &windowed_height);

    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    if (mode == nullptr) {
      return;
    }

    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height,
                         mode->refreshRate);
    fullscreen = true;
  } else {
    glfwSetWindowMonitor(window, nullptr, windowed_pos_x, windowed_pos_y,
                         windowed_width > 0 ? windowed_width : 800,
                         windowed_height > 0 ? windowed_height : 600, 0);
    fullscreen = false;
  }
}

void GuiManager::toggle_fullscreen() { set_fullscreen(!fullscreen); }
