include_guard(GLOBAL)

add_library(imgui_lib
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui_lib PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui_lib PUBLIC
  glfw
  OpenGL::GL
)

set(MEGATOY_SOURCES
  src/main.cpp
  src/app_state.cpp
  src/audio_manager.cpp
  src/gui_manager.cpp
  src/graphics/texture_utils.cpp
  src/graphics/stb_image_impl.cpp
  src/preference_manager.cpp
  src/preference_storage_json.cpp
  src/channel_allocator.cpp
  src/resource_manager.cpp
  src/history/history_manager.cpp
  src/midi_usb.cpp
  src/patches/patch_repository.cpp
  src/platform/file_dialog.cpp
  src/formats/common.cpp
  src/formats/ctrmml.cpp
  src/formats/dmp.cpp
  src/formats/gin.cpp
  src/parsers/fui_parser.cpp
  src/parsers/rym2612_parser.cpp
  src/ui/preview/preview.cpp
  src/ui/preview/algorithm_preview.cpp
  src/ui/preview/ssg_preview.cpp
  src/ui/keyboard_typing.cpp
  src/ui/styles/theme.cpp
  src/ui/main_menu.cpp
  src/ui/patch_editor.cpp
  src/ui/operator_editor.cpp
  src/ui/midi_keyboard.cpp
  src/ui/patch_selector.cpp
  src/ui/mml_console.cpp
  src/ui/waveform.cpp
  src/ui/preferences.cpp
  src/ui/styles/megatoy_style.cpp
  src/ym2612/device.cpp
  src/ym2612/channel.cpp
  src/ym2612/operator.cpp
  src/ym2612/wave_sampler.cpp
)

if(WIN32)
  list(APPEND MEGATOY_SOURCES src/platform/windows_entry.cpp)
endif()

if(APPLE)
  add_executable(megatoy MACOSX_BUNDLE ${MEGATOY_SOURCES})
elseif(WIN32)
  add_executable(megatoy WIN32 ${MEGATOY_SOURCES})
else()
  add_executable(megatoy ${MEGATOY_SOURCES})
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_options(megatoy PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:
          -O3 -ffast-math -funroll-loops -march=native>
        $<$<CXX_COMPILER_ID:MSVC>:/O2 /fp:fast>
    )
endif()

target_link_libraries(megatoy PRIVATE
  vgm-player vgm-audio
  imgui_lib
  glfw
  OpenGL::GL
  nlohmann_json::nlohmann_json
  nfd
  rtmidi
)

if(UNIX AND NOT APPLE)
  find_package(X11 REQUIRED)
  target_link_libraries(megatoy PRIVATE ${X11_LIBRARIES})
endif()

target_include_directories(megatoy PRIVATE
  ${libvgm_SOURCE_DIR}
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${stb_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(megatoy PRIVATE
  VGM_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
  USE_EMBEDDED_RESOURCES
  $<$<PLATFORM_ID:Darwin>:GL_SILENCE_DEPRECATION>
)

add_embedded_assets(megatoy
  EXCLUDE_PATTERNS "\\.DS_Store$" "\\.ase$" "\\.gitkeep$"
)

target_include_directories(megatoy PRIVATE ${CMAKE_BINARY_DIR})

add_custom_command(
    TARGET megatoy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove -f
            ${CMAKE_SOURCE_DIR}/compile_commands.json
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_SOURCE_DIR}/compile_commands.json
)
