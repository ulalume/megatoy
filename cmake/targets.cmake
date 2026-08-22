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
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  target_compile_definitions(imgui_lib PUBLIC IMGUI_IMPL_OPENGL_ES3 IMGUI_IMPL_OPENGL_LOADER_CUSTOM)
  # Up to Emscripten 6.0.3, selecting WebAssembly SIMD instructions for
  # ImGui::NewFrame sent LLVM into unbounded recursion in
  # SelectionDAG::isKnownNeverNaN and crashed the compiler -- or, under LTO
  # where codegen runs in the linker, wasm-ld. Only imgui.cpp was affected,
  # and only at -O2 or above with -msimd128.
  #
  # 6.0.4 fixes it upstream. This is kept so older SDKs still build: SIMD
  # earns its keep in the audio path (ymfm, the resampler, kissfft), not in
  # ImGui's layout code, so dropping it here costs nothing measurable.
  target_compile_options(imgui_lib PRIVATE -mno-simd128)
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
target_link_libraries(imgui_lib PUBLIC OpenGL::GL)
endif()

# ymfm: only the OPN family is needed, but ymfm_opn.cpp pulls in the ADPCM and
# SSG engines that the YM2610/YM2608 share, so all three must be compiled.
add_library(ymfm_core STATIC
  ${ymfm_SOURCE_DIR}/src/ymfm_opn.cpp
  ${ymfm_SOURCE_DIR}/src/ymfm_adpcm.cpp
  ${ymfm_SOURCE_DIR}/src/ymfm_ssg.cpp
)
target_include_directories(ymfm_core PUBLIC ${ymfm_SOURCE_DIR}/src)

# Sample rate conversion, vendored from libvgm (src/audio/resampler). Kept as
# a separate C target so the project-wide C++ standard is not applied to it.
add_library(megatoy_resampler STATIC
  src/audio/resampler/Resampler.c
  src/audio/resampler/logging.c
)
target_compile_definitions(megatoy_resampler PUBLIC HAVE_STDINT_H)
target_include_directories(megatoy_resampler PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Fetched via FetchContent (cmake/deps.cmake); bump the pin there to follow
# upstream. Every packaging path below flows through this variable.
set(MEGATOY_PRESETS_SOURCE_DIR "${ym2612_patches_SOURCE_DIR}")

set(MEGATOY_CORE_SOURCES
  src/app_services.cpp
  src/app_state.cpp
  src/audio/audio_engine.cpp
  src/audio/scope_buffer.cpp
  src/audio/scope_trigger.cpp
  src/audio/spectrum_analyzer.cpp
  src/audio/audio_manager.cpp
  src/audio/sdl_audio_transport.cpp
  src/channel_allocator.cpp
  src/core/random_utils.cpp
  src/core/status.cpp
  src/core/utf8_utils.cpp
  src/formats/ginpkg.cpp
  src/formats/patch_loader.cpp
  src/formats/ym2612_format_adapter.cpp
  src/formats/patch_registry.cpp
  src/gui/components/about_dialog.cpp
  src/gui/components/confirmation_dialog.cpp
  src/gui/components/envelope_image.cpp
  src/gui/input/keyboard_typing.cpp
  src/gui/components/main_menu.cpp
  src/gui/components/midi_keyboard.cpp
  src/gui/components/mml_console.cpp
  src/gui/components/operator_commands.cpp
  src/gui/components/operator_editor.cpp
  src/gui/components/operator_selection.cpp
  src/gui/components/patch_drop.cpp
  src/gui/components/patch_editor.cpp
  src/gui/components/patch_filter.cpp
  src/gui/components/patch_lab_window.cpp
  src/gui/components/patch_selector.cpp
  src/gui/components/patch_selector_shared.cpp
  src/gui/components/patch_table_view.cpp
  src/gui/components/patch_tree_flatten.cpp
  src/gui/components/patch_tree_view.cpp
  src/gui/components/preferences.cpp
  src/gui/save_export_actions.cpp
  src/gui/components/status_toasts.cpp
  src/gui/components/preview/algorithm_preview.cpp
  src/gui/components/preview/preview.cpp
  src/gui/components/preview/ssg_preview.cpp
  src/gui/components/waveform.cpp
  src/gui/gui_manager.cpp
  src/gui/ui_renderer.cpp
  src/gui/image/resource_manager.cpp
  src/gui/image/stb_image_impl.cpp
  src/gui/image/texture_utils.cpp
  src/gui/imgui_ini_bridge.cpp
  src/platform/clipboard.cpp
  src/gui/styles/megatoy_style.cpp
  src/gui/styles/theme.cpp
  src/history/history_manager.cpp
  src/drop_actions.cpp
  src/midi/midi_input_manager.cpp

  src/patches/patch_session.cpp
  src/patches/filename_utils.cpp
  src/workspace/path_policy.cpp
  src/workspace/workspace.cpp
  src/patches/patch_lab.cpp
  src/patches/persistent_parse_cache.cpp
  src/patches/patch_repository.cpp
  src/patches/patch_write.cpp
  src/patches/filesystem_patch_storage.cpp
  src/patches/folder_metadata.cpp
  src/platform/file_dialog.cpp
  src/preferences/preference_manager.cpp
  src/preferences/preference_storage_json.cpp

  src/platform/std_file_system.cpp
  src/platform/import_pipeline.cpp
  src/system/path_service.cpp
  src/update/update_checker.cpp
  src/update/release_provider.cpp
  src/update/version.cpp
  src/ym2612/channel.cpp
  src/ym2612/device.cpp
  src/ym2612/operator.cpp
  src/ym2612/operator_edit.cpp
  src/ym2612/ymfm_chip.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  list(APPEND MEGATOY_CORE_SOURCES
    src/audio/webaudio_transport.cpp
    src/gui/imgui_ini_bridge_web.cpp
    src/platform/web/web_platform_services.cpp
    src/platform/web/web_file_system.cpp
    src/platform/web/web_download.cpp
    src/platform/web/local_storage.cpp
    src/platform/web/web_folder_delete.cpp
    src/platform/web/web_folder_import.cpp
    src/platform/web/web_storage_bootstrap.cpp
    src/platform/web/web_storage_persistence.cpp
    src/platform/web/web_workspace_download.cpp
    src/platform/web/web_patch_url.cpp
    src/platform/web/web_midi_backend.cpp
  )
else()
  list(APPEND MEGATOY_CORE_SOURCES
    src/gui/components/folder_scan_dialog.cpp
    src/midi/rtmidi_backend.cpp
    src/patches/background_folder_scan.cpp
    src/patches/legacy_metadata_migration.cpp
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
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${stb_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_BINARY_DIR}
  ${iconfontcppheaders_SOURCE_DIR}
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(MEGATOY_CORE_RELEASE_FLAGS -O3 -ffast-math -funroll-loops)
  if(MEGATOY_RELEASE_CPU_FLAGS)
    list(APPEND MEGATOY_CORE_RELEASE_FLAGS ${MEGATOY_RELEASE_CPU_FLAGS})
  endif()
  target_compile_options(megatoy_core PRIVATE
      $<$<CXX_COMPILER_ID:GNU,Clang>:${MEGATOY_CORE_RELEASE_FLAGS}>
      $<$<CXX_COMPILER_ID:MSVC>:/O2 /fp:fast>
  )
endif()

target_link_libraries(megatoy_core PUBLIC
  ymfm_core
  megatoy_resampler
  imgui_lib
  SDL3::SDL3
  nlohmann_json::nlohmann_json
  chord_detector::chord_detector
  kissfft
  miniz
  ym2612_format
)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
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
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
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
  # CMake's default Info.plist template fills these from MACOSX_BUNDLE_*
  # variables, and leaves them as empty strings when they are not set.
  #
  # An empty CFBundleIdentifier is not merely cosmetic: AppKit refuses to run
  # NSOpenPanel/NSSavePanel for such a bundle, so [dialog runModal] returns
  # immediately without ever showing a window. That is what made the native
  # file dialogs look broken on macOS.
  set_target_properties(megatoy PROPERTIES
    MACOSX_BUNDLE_ICON_FILE "icon.icns"
    MACOSX_BUNDLE_BUNDLE_NAME "megatoy"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.${MEGATOY_GITHUB_USER}.megatoy"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_INFO_STRING "megatoy ${PROJECT_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "MIT License"
  )
endif()

install(TARGETS megatoy
  BUNDLE DESTINATION .
  RUNTIME DESTINATION .
)

if(UNIX AND NOT APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  find_package(X11 REQUIRED)
  target_link_libraries(megatoy PRIVATE ${X11_LIBRARIES})
endif()

add_embedded_assets(megatoy
  EXCLUDE_PATTERNS "\\.DS_Store$" "\\.ase$" "\\.gitkeep$" "^presets/" "\\.txt$"
)

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set_target_properties(megatoy PROPERTIES SUFFIX ".html")
  target_link_options(megatoy PRIVATE
    "--bind"
    "-sFORCE_FILESYSTEM=1"
    "-sFULL_ES3=1"
    "-sMAX_WEBGL_VERSION=2"
    "-sMIN_WEBGL_VERSION=2"
    "-sWASM=1"
    "-sINITIAL_MEMORY=134217728"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sMAXIMUM_MEMORY=1073741824"
    "-fwasm-exceptions"
    "-sWASM_LEGACY_EXCEPTIONS=0"
    # IDBFS backs /megatoy with IndexedDB so the patch library survives a
    # reload; dist/web_pre.js mounts it before main() runs.
    "-lidbfs.js"
    "--pre-js" "${CMAKE_SOURCE_DIR}/dist/web_pre.js"
    # main() holds AppServices by value, and Emscripten's default stack is a
    # mere 64 KB. Give it room so a new member does not overflow the stack at
    # startup, which surfaces only as "memory access out of bounds".
    "-sSTACK_SIZE=1048576"
    # Each --preload-file below uses the SHELL: prefix (CMake >= 3.12) rather
    # than either "--preload-file" "value" as two list entries, or
    # "--preload-file value" as one. Two entries breaks because
    # target_link_options de-duplicates repeated list entries by exact
    # string match, and plain "--preload-file" would collide across the two
    # uses below, silently dropping the second flag while leaving both of
    # its values on the command line as bare (unrecognized) arguments. One
    # combined entry without SHELL: also breaks, the opposite way: CMake
    # then quotes it as a single atomic shell word (verified via
    # EMCC_DEBUG=1), so em++ receives "--preload-file value" as ONE argv
    # token, which its argument parser does not recognize as the
    # --preload-file flag at all -- it is silently dropped, no .data file is
    # produced, and no error is printed. SHELL: sidesteps both failure modes:
    # it is exempt from the de-dup pass, and CMake re-splits its content into
    # separate argv words for the underlying build tool.
    "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets@/app/assets"
    # Presets no longer live under assets/ on disk (they are fetched via
    # FetchContent -- see MEGATOY_PRESETS_SOURCE_DIR above), so they need
    # their own preload to keep landing at the same MEMFS path,
    # /app/assets/presets, that src/system/path_service.cpp expects.
    "SHELL:--preload-file ${MEGATOY_PRESETS_SOURCE_DIR}@/app/assets/presets"
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
