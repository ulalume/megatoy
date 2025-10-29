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

set(MEGATOY_CORE_SOURCES
  src/app_services.cpp
  src/app_state.cpp
  src/audio/audio_manager.cpp
  src/channel_allocator.cpp
  src/formats/common.cpp
  src/formats/ctrmml.cpp
  src/formats/dmp.cpp
  src/formats/gin.cpp
  src/formats/fui.cpp
  src/formats/patch_loader.cpp
  src/formats/rym2612.cpp
  src/gui/components/confirmation_dialog.cpp
  src/gui/components/envelope_image.cpp
  src/gui/input/keyboard_typing.cpp
  src/gui/components/main_menu.cpp
  src/gui/components/midi_keyboard.cpp
  src/gui/components/mml_console.cpp
  src/gui/components/operator_editor.cpp
  src/gui/components/patch_drop.cpp
  src/gui/components/patch_editor.cpp
  src/gui/components/patch_selector.cpp
  src/gui/components/preferences.cpp
  src/gui/components/preview/algorithm_preview.cpp
  src/gui/components/preview/preview.cpp
  src/gui/components/preview/ssg_preview.cpp
  src/gui/components/waveform.cpp
  src/gui/gui_manager.cpp
  src/gui/ui_renderer.cpp
  src/gui/image/resource_manager.cpp
  src/gui/image/stb_image_impl.cpp
  src/gui/image/texture_utils.cpp
  src/gui/styles/megatoy_style.cpp
  src/gui/styles/theme.cpp
  src/history/history_manager.cpp
  src/drop_actions.cpp
  src/midi/midi_input_manager.cpp

  src/patches/patch_session.cpp
  src/patches/patch_repository.cpp
  src/patches/patch_metadata.cpp
  src/platform/file_dialog.cpp
  src/preferences/preference_manager.cpp
  src/preferences/preference_storage_json.cpp
  src/system/path_service.cpp
  src/ym2612/channel.cpp
  src/ym2612/device.cpp
  src/ym2612/operator.cpp
  src/ym2612/wave_sampler.cpp
  src/ym2612/fft_analyzer.cpp
)
add_library(megatoy_core ${MEGATOY_CORE_SOURCES})

target_include_directories(megatoy_core PUBLIC
  ${libvgm_SOURCE_DIR}
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${stb_SOURCE_DIR}
  ${SQLiteCpp_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
  ${iconfontcppheaders_SOURCE_DIR}
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_options(megatoy_core PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:
          -O3 -ffast-math -funroll-loops -march=native>
        $<$<CXX_COMPILER_ID:MSVC>:/O2 /fp:fast>
    )
endif()

target_link_libraries(megatoy_core PUBLIC
  vgm-player vgm-audio
  imgui_lib
  glfw
  OpenGL::GL
  nlohmann_json::nlohmann_json
  nfd
  rtmidi
  chord_detector::chord_detector
  kissfft
  SQLiteCpp
)

set(MEGATOY_PRESETS_RELATIVE_PATH_VALUE "presets")
if(APPLE)
  set(MEGATOY_PRESETS_RELATIVE_PATH_VALUE "../Resources/presets")
endif()

target_compile_definitions(megatoy_core PUBLIC
  VGM_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
  USE_EMBEDDED_RESOURCES
  MEGATOY_PRESETS_RELATIVE_PATH="${MEGATOY_PRESETS_RELATIVE_PATH_VALUE}"
  $<$<PLATFORM_ID:Darwin>:GL_SILENCE_DEPRECATION>
)

set(MEGATOY_MAIN_SOURCES src/main.cpp)
if(WIN32)
  list(APPEND MEGATOY_MAIN_SOURCES src/platform/windows_entry.cpp)
endif()

if(APPLE)
  add_executable(megatoy MACOSX_BUNDLE ${MEGATOY_MAIN_SOURCES})
elseif(WIN32)
  add_executable(megatoy WIN32 ${MEGATOY_MAIN_SOURCES})
else()
  add_executable(megatoy ${MEGATOY_MAIN_SOURCES})
endif()

target_link_libraries(megatoy PRIVATE megatoy_core)

install(TARGETS megatoy
  BUNDLE DESTINATION .
  RUNTIME DESTINATION .
)

if(UNIX AND NOT APPLE)
  find_package(X11 REQUIRED)
  target_link_libraries(megatoy PRIVATE ${X11_LIBRARIES})
endif()

add_embedded_assets(megatoy
  EXCLUDE_PATTERNS "\\.DS_Store$" "\\.ase$" "\\.gitkeep$" "^presets/" "\\.txt$"
)

target_include_directories(megatoy PRIVATE ${CMAKE_BINARY_DIR})

add_executable(subsystem_tests tests/subsystem_tests.cpp)
target_link_libraries(subsystem_tests PRIVATE megatoy_core)

add_test(NAME subsystem_tests COMMAND subsystem_tests)

set(COMPILE_COMMANDS_BINARY "${CMAKE_BINARY_DIR}/compile_commands.json")
set(COMPILE_COMMANDS_SOURCE "${CMAKE_SOURCE_DIR}/compile_commands.json")

if(WIN32)
  # Windows builds typically lack symlink privileges, so fall back to copying.
  set(COMPILE_COMMANDS_MIRROR_COMMAND copy_if_different)
else()
  set(COMPILE_COMMANDS_MIRROR_COMMAND create_symlink)
endif()

set(COMPILE_COMMANDS_MIRROR_SCRIPT
    "${CMAKE_SOURCE_DIR}/cmake/update_compile_commands.cmake")

add_custom_command(
    TARGET megatoy POST_BUILD
    COMMAND ${CMAKE_COMMAND}
            -Dsrc="${COMPILE_COMMANDS_BINARY}"
            -Ddst="${COMPILE_COMMANDS_SOURCE}"
            -Dmirror_command=${COMPILE_COMMANDS_MIRROR_COMMAND}
            -P "${COMPILE_COMMANDS_MIRROR_SCRIPT}"
)

set(MEGATOY_PRESETS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/assets/presets")
if(EXISTS "${MEGATOY_PRESETS_SOURCE_DIR}")
  if(APPLE)
    set(MEGATOY_PRESETS_DESTINATION
        "$<TARGET_BUNDLE_CONTENT_DIR:megatoy>/Resources/presets")
  else()
    set(MEGATOY_PRESETS_DESTINATION "$<TARGET_FILE_DIR:megatoy>/presets")
  endif()

  add_custom_command(
      TARGET megatoy POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E remove_directory
              "${MEGATOY_PRESETS_DESTINATION}"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${MEGATOY_PRESETS_SOURCE_DIR}"
              "${MEGATOY_PRESETS_DESTINATION}"
  )

  # Ensure presets are part of install / CPack artefacts on every platform.
  install(
    DIRECTORY "${MEGATOY_PRESETS_SOURCE_DIR}/"
    DESTINATION "$<IF:$<PLATFORM_ID:Darwin>,Resources/presets,presets>"
  )
else()
  message(WARNING
    "Presets directory not found at ${MEGATOY_PRESETS_SOURCE_DIR}")
endif()
