include_guard(GLOBAL)

add_library(imgui_lib
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui_lib PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui_lib PUBLIC
  SDL3::SDL3
)
if(EMSCRIPTEN)
  target_compile_definitions(imgui_lib PUBLIC IMGUI_IMPL_OPENGL_ES3 IMGUI_IMPL_OPENGL_LOADER_CUSTOM)
endif()
if(NOT EMSCRIPTEN)
target_link_libraries(imgui_lib PUBLIC OpenGL::GL)
endif()

set(MEGATOY_PRESETS_SOURCE_DIR "${CMAKE_SOURCE_DIR}/assets/presets")

set(MEGATOY_CORE_SOURCES
  src/app_services.cpp
  src/app_state.cpp
  src/audio/audio_engine.cpp
  src/audio/audio_manager.cpp
  src/audio/sdl_audio_transport.cpp
  src/channel_allocator.cpp
  src/formats/common.cpp
  src/formats/ctrmml.cpp
  src/formats/dmp.cpp
  src/formats/gin.cpp
  src/formats/ginpkg.cpp
  src/formats/fui.cpp
  src/formats/patch_loader.cpp
  src/formats/rym2612.cpp
  src/gui/components/about_dialog.cpp
  src/gui/components/confirmation_dialog.cpp
  src/gui/components/envelope_image.cpp
  src/gui/input/keyboard_typing.cpp
  src/gui/components/main_menu.cpp
  src/gui/components/midi_keyboard.cpp
  src/gui/components/mml_console.cpp
  src/gui/components/operator_editor.cpp
  src/gui/components/patch_drop.cpp
  src/gui/components/patch_editor.cpp
  src/gui/components/patch_history.cpp
  src/gui/components/patch_lab_window.cpp
  src/gui/components/patch_selector.cpp
  src/gui/components/preferences.cpp
  src/gui/save_export_actions.cpp
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
  src/patches/patch_lab.cpp
  src/patches/patch_repository.cpp
  src/patches/patch_metadata.cpp
  src/platform/file_dialog.cpp
  src/preferences/preference_manager.cpp
  src/preferences/preference_storage_json.cpp

  src/platform/std_file_system.cpp
  src/system/path_service.cpp
  src/update/update_checker.cpp
  src/update/release_provider.cpp
  src/ym2612/channel.cpp
  src/ym2612/device.cpp
  src/ym2612/operator.cpp
  src/ym2612/wave_sampler.cpp
  src/ym2612/fft_analyzer.cpp
)

if(EMSCRIPTEN)
  list(APPEND MEGATOY_CORE_SOURCES
    src/audio/webaudio_transport.cpp
    src/platform/web/web_platform_services.cpp
    src/platform/web/web_file_system.cpp
    src/platform/web/web_download.cpp
    src/platform/web/local_storage.cpp
    src/platform/web/web_patch_store.cpp
    src/platform/web/web_midi_backend.cpp
  )
else()
  list(APPEND MEGATOY_CORE_SOURCES
    src/midi/rtmidi_backend.cpp
    src/platform/native/native_file_system.cpp
    src/platform/native/desktop_platform_services.cpp
  )
endif()
# Add platform-specific source files
if(APPLE)
  list(APPEND MEGATOY_CORE_SOURCES src/system/open_default_browser.mm)
elseif(WIN32)
  list(APPEND MEGATOY_CORE_SOURCES src/system/open_default_browser.cpp)
else()
  list(APPEND MEGATOY_CORE_SOURCES src/system/open_default_browser.cpp)
endif()

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
  set(MEGATOY_CORE_RELEASE_FLAGS -O3 -ffast-math -funroll-loops)
  if(MEGATOY_RELEASE_CPU_FLAGS)
    list(APPEND MEGATOY_CORE_RELEASE_FLAGS ${MEGATOY_RELEASE_CPU_FLAGS})
  elseif(NOT EMSCRIPTEN)
    list(APPEND MEGATOY_CORE_RELEASE_FLAGS -march=native)
  endif()
  target_compile_options(megatoy_core PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang>:${MEGATOY_CORE_RELEASE_FLAGS}>
      $<$<CXX_COMPILER_ID:MSVC>:/O2 /fp:fast>
  )
endif()

target_link_libraries(megatoy_core PUBLIC
  vgm-player
  imgui_lib
  SDL3::SDL3
  nlohmann_json::nlohmann_json
  chord_detector::chord_detector
  kissfft
  miniz
)

if(NOT EMSCRIPTEN)
  target_link_libraries(megatoy_core PUBLIC
    OpenGL::GL
    CURL::libcurl
    nfd
    rtmidi
    SQLiteCpp
  )
  if(WIN32)
    target_link_libraries(megatoy_core PUBLIC shell32)
  endif()
endif()

if(APPLE)
  target_link_libraries(megatoy_core PUBLIC
    "-framework AppKit"
    "-framework ApplicationServices"
    "-framework CoreFoundation"
    "-framework Foundation"
  )
endif()

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
if(EMSCRIPTEN)
  target_compile_definitions(megatoy_core PUBLIC IMGUI_IMPL_OPENGL_ES3)
endif()

set(MEGATOY_MAIN_SOURCES src/main.cpp)
if(WIN32)
  list(APPEND MEGATOY_MAIN_SOURCES src/platform/windows_entry.cpp)
  set(MEGATOY_ICON_RC "${CMAKE_BINARY_DIR}/megatoy_icon.rc")
  configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/windows_icon.rc.in"
    "${MEGATOY_ICON_RC}"
    @ONLY
  )
  list(APPEND MEGATOY_MAIN_SOURCES "${MEGATOY_ICON_RC}")
endif()

if(APPLE)
  add_executable(megatoy MACOSX_BUNDLE ${MEGATOY_MAIN_SOURCES})
elseif(WIN32)
  add_executable(megatoy WIN32 ${MEGATOY_MAIN_SOURCES})
else()
  add_executable(megatoy ${MEGATOY_MAIN_SOURCES})
endif()

target_link_libraries(megatoy PRIVATE megatoy_core)

if(TARGET SDL3::SDL3main)
  target_link_libraries(megatoy PRIVATE SDL3::SDL3main)
endif()

if(APPLE)
  set(MEGATOY_BUNDLE_ICON "${CMAKE_SOURCE_DIR}/dist/icon.icns")
  set_source_files_properties(${MEGATOY_BUNDLE_ICON}
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  target_sources(megatoy PRIVATE ${MEGATOY_BUNDLE_ICON})
  set_target_properties(megatoy PROPERTIES MACOSX_BUNDLE_ICON_FILE "icon.icns")
endif()

install(TARGETS megatoy
  BUNDLE DESTINATION .
  RUNTIME DESTINATION .
)

if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
  find_package(X11 REQUIRED)
  target_link_libraries(megatoy PRIVATE ${X11_LIBRARIES})
endif()

add_embedded_assets(megatoy
  EXCLUDE_PATTERNS "\\.DS_Store$" "\\.ase$" "\\.gitkeep$" "^presets/" "\\.txt$"
)

if(EMSCRIPTEN)
  set_target_properties(megatoy PROPERTIES SUFFIX ".html")
  target_link_options(megatoy PRIVATE
    "--bind"
    "-sFORCE_FILESYSTEM=1"
    "-sUSE_ZLIB=1"
    "-sFULL_ES3=1"
    "-sMAX_WEBGL_VERSION=2"
    "-sMIN_WEBGL_VERSION=2"
    "-sWASM=1"
    "-sINITIAL_MEMORY=134217728"
    "--preload-file" "${CMAKE_SOURCE_DIR}/assets@/app/assets"
  )
  if(MEGATOY_GENERATE_SIMPLE_HTML)
    set(MEGATOY_SIMPLE_SHELL_SRC "${CMAKE_SOURCE_DIR}/dist/web_shell_simple.html")
    set(MEGATOY_SIMPLE_ICON_SRC "${CMAKE_SOURCE_DIR}/dist/icon.ico")
    add_custom_command(
      TARGET megatoy POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${MEGATOY_SIMPLE_SHELL_SRC}"
              "$<TARGET_FILE_DIR:megatoy>/megatoy_simple.html"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${MEGATOY_SIMPLE_ICON_SRC}"
              "$<TARGET_FILE_DIR:megatoy>/icon.ico"
      COMMENT "Copying minimal Web shell to megatoy_simple.html"
    )
  endif()

  set(MEGATOY_WEB_DIST_DIR "${CMAKE_BINARY_DIR}/web_dist")
  if(EXISTS "${MEGATOY_PRESETS_SOURCE_DIR}")
    set(MEGATOY_WEB_PRESETS_COMMAND
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${MEGATOY_PRESETS_SOURCE_DIR}"
                "${MEGATOY_WEB_DIST_DIR}/presets")
  else()
    set(MEGATOY_WEB_PRESETS_COMMAND
        COMMAND ${CMAKE_COMMAND} -E echo
                "Skipping presets copy; not found at ${MEGATOY_PRESETS_SOURCE_DIR}")
  endif()
  add_custom_command(
    TARGET megatoy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${MEGATOY_WEB_DIST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MEGATOY_WEB_DIST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:megatoy>/icon.ico"
            "${MEGATOY_WEB_DIST_DIR}/icon.ico"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:megatoy>/megatoy_simple.html"
            "${MEGATOY_WEB_DIST_DIR}/index.html"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:megatoy>/megatoy.js"
            "${MEGATOY_WEB_DIST_DIR}/megatoy.js"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:megatoy>/megatoy.wasm"
            "${MEGATOY_WEB_DIST_DIR}/megatoy.wasm"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:megatoy>/megatoy.data"
            "${MEGATOY_WEB_DIST_DIR}/megatoy.data"
    ${MEGATOY_WEB_PRESETS_COMMAND}
    COMMENT "Preparing web distribution files"
  )
endif()

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

if(UNIX AND NOT APPLE)
  add_custom_command(
      TARGET megatoy POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CMAKE_SOURCE_DIR}/dist/icon.png"
              "$<TARGET_FILE_DIR:megatoy>/icon.png"
  )
  install(FILES "${CMAKE_SOURCE_DIR}/dist/icon.png" DESTINATION .)
endif()
