cmake_minimum_required(VERSION 3.24)

set(MEGATOY_VERSION "0.8.1" CACHE STRING "megatoy application version")
project(megatoy VERSION ${MEGATOY_VERSION})

# Objective-C sources are used for macOS integrations
if(APPLE)
  enable_language(OBJC OBJCXX)
endif()

# Project information
set(MEGATOY_VERSION_TAG "v${PROJECT_VERSION}")

# GitHub repository information
set(MEGATOY_GITHUB_USER "ulalume" CACHE STRING "GitHub username")
set(MEGATOY_GITHUB_REPO "megatoy" CACHE STRING "GitHub repository name")

configure_file(
  "${CMAKE_SOURCE_DIR}/src/project_info.hpp.in"
  "${CMAKE_BINARY_DIR}/project_info.hpp"
  @ONLY
)

enable_testing()

# Set Release as the default build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Compiler-specific optimization settings
set(MEGATOY_LINUX_X86_64 FALSE)
set(MEGATOY_RELEASE_CPU_FLAGS "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
  set(MEGATOY_LINUX_X86_64 TRUE)
  option(MEGATOY_GENERAL_X86_64_LINUX "Build with generic x86-64 flags on Linux" OFF)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  message(STATUS "Setting up Release build optimizations")
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(OPTIMIZATION_FLAGS
        "-O3"
        "-DNDEBUG"
        "-funroll-loops"
    )
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
      if(MEGATOY_LINUX_X86_64)
        if(MEGATOY_GENERAL_X86_64_LINUX)
          message(STATUS "Using generic x86-64 tuning for Linux Release build")
          set(MEGATOY_RELEASE_CPU_FLAGS "-march=x86-64" "-mtune=generic")
        else()
          message(STATUS "Using native CPU optimizations for Linux Release build")
          set(MEGATOY_RELEASE_CPU_FLAGS "-march=native")
        endif()
      endif()
    endif()
    if(MEGATOY_RELEASE_CPU_FLAGS)
      list(APPEND OPTIMIZATION_FLAGS ${MEGATOY_RELEASE_CPU_FLAGS})
    endif()
    list(APPEND OPTIMIZATION_FLAGS
        "-flto"
        "-fomit-frame-pointer"
    )
    string(JOIN " " OPTIMIZATION_STRING ${OPTIMIZATION_FLAGS})
    set(CMAKE_C_FLAGS_RELEASE "${OPTIMIZATION_STRING}")
    set(CMAKE_CXX_FLAGS_RELEASE "${OPTIMIZATION_STRING}")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto")
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG /fp:fast /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG")
  endif()
  message(STATUS "Release CXX flags: ${CMAKE_CXX_FLAGS_RELEASE}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Define M_PI for Windows builds
if(WIN32)
  add_compile_definitions(_USE_MATH_DEFINES)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  add_compile_options("-fwasm-exceptions" "-sWASM_LEGACY_EXCEPTIONS=0")
endif()

include(cmake/deps.cmake)
include(cmake/embed_assets.cmake)

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msimd128")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msimd128")
  option(MEGATOY_GENERATE_SIMPLE_HTML
         "Copy a minimal HTML launcher (megatoy_simple.html) alongside megatoy.html"
         ON)
  # Ensure libc++ headers are searched before any sysroot includes injected by dependencies.
  foreach(_inc_dir ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
    if(_inc_dir MATCHES "/include/c\\+\\+/v1$")
      include_directories(BEFORE SYSTEM "${_inc_dir}")
      break()
    endif()
  endforeach()
endif()

include(cmake/targets.cmake)

# Tests
enable_testing()
add_executable(random_utils_test tests/core/random_utils_test.cpp src/core/random_utils.cpp)
target_include_directories(random_utils_test PRIVATE src)
add_test(NAME random_utils_test COMMAND random_utils_test)
add_executable(patch_registry_test tests/core/patch_registry_test.cpp)
target_include_directories(patch_registry_test PRIVATE src)
target_link_libraries(patch_registry_test PRIVATE megatoy_core)
add_test(NAME patch_registry_test COMMAND patch_registry_test)
add_executable(patch_io_roundtrip_test tests/core/patch_io_roundtrip_test.cpp)
target_include_directories(patch_io_roundtrip_test PRIVATE src)
target_link_libraries(patch_io_roundtrip_test PRIVATE megatoy_core)
add_test(NAME patch_io_roundtrip_test COMMAND patch_io_roundtrip_test)
add_executable(ym2612_render_test tests/audio/ym2612_render_test.cpp)
target_include_directories(ym2612_render_test PRIVATE src)
target_link_libraries(ym2612_render_test PRIVATE megatoy_core)
add_test(NAME ym2612_render_test COMMAND ym2612_render_test)
add_executable(analyzer_test tests/audio/analyzer_test.cpp)
target_include_directories(analyzer_test PRIVATE src)
target_link_libraries(analyzer_test PRIVATE megatoy_core)
add_test(NAME analyzer_test COMMAND analyzer_test)
add_executable(command_queue_test tests/audio/command_queue_test.cpp)
target_include_directories(command_queue_test PRIVATE src)
target_link_libraries(command_queue_test PRIVATE megatoy_core)
add_test(NAME command_queue_test COMMAND command_queue_test)
add_executable(performance_test tests/audio/performance_test.cpp)
target_include_directories(performance_test PRIVATE src)
target_link_libraries(performance_test PRIVATE megatoy_core)
add_test(NAME performance_test COMMAND performance_test)
add_executable(ginpkg_history_test tests/formats/ginpkg_history_test.cpp)
target_include_directories(ginpkg_history_test PRIVATE src)
target_link_libraries(ginpkg_history_test PRIVATE megatoy_core)
add_test(NAME ginpkg_history_test COMMAND ginpkg_history_test)
add_executable(vgm_multi_instrument_test tests/formats/vgm_multi_instrument_test.cpp)
target_include_directories(vgm_multi_instrument_test PRIVATE src)
target_link_libraries(vgm_multi_instrument_test PRIVATE megatoy_core)
add_test(NAME vgm_multi_instrument_test COMMAND vgm_multi_instrument_test)
add_executable(patch_write_test tests/patches/patch_write_test.cpp)
target_include_directories(patch_write_test PRIVATE src)
target_link_libraries(patch_write_test PRIVATE megatoy_core)
add_test(NAME patch_write_test COMMAND patch_write_test)
add_executable(filename_utils_test tests/patches/filename_utils_test.cpp)
target_include_directories(filename_utils_test PRIVATE src)
target_link_libraries(filename_utils_test PRIVATE megatoy_core)
add_test(NAME filename_utils_test COMMAND filename_utils_test)
add_executable(utf8_utils_test tests/core/utf8_utils_test.cpp)
target_include_directories(utf8_utils_test PRIVATE src)
target_link_libraries(utf8_utils_test PRIVATE megatoy_core)
add_test(NAME utf8_utils_test COMMAND utf8_utils_test)
add_executable(patch_repository_delete_test
  tests/patches/patch_repository_delete_test.cpp)
target_include_directories(patch_repository_delete_test PRIVATE src)
target_link_libraries(patch_repository_delete_test PRIVATE megatoy_core)
add_test(NAME patch_repository_delete_test COMMAND patch_repository_delete_test)
add_executable(patch_repository_rename_test
  tests/patches/patch_repository_rename_test.cpp)
target_include_directories(patch_repository_rename_test PRIVATE src)
target_link_libraries(patch_repository_rename_test PRIVATE megatoy_core)
add_test(NAME patch_repository_rename_test COMMAND patch_repository_rename_test)
add_executable(folder_metadata_test tests/patches/folder_metadata_test.cpp)
target_include_directories(folder_metadata_test PRIVATE src)
target_link_libraries(folder_metadata_test PRIVATE megatoy_core)
add_test(NAME folder_metadata_test COMMAND folder_metadata_test)
add_executable(patch_tree_flatten_test tests/gui/patch_tree_flatten_test.cpp)
target_include_directories(patch_tree_flatten_test PRIVATE src)
target_link_libraries(patch_tree_flatten_test PRIVATE megatoy_core)
add_test(NAME patch_tree_flatten_test COMMAND patch_tree_flatten_test)
add_executable(operator_layout_test tests/ym2612/operator_layout_test.cpp)
target_include_directories(operator_layout_test PRIVATE src)
target_link_libraries(operator_layout_test PRIVATE megatoy_core)
add_test(NAME operator_layout_test COMMAND operator_layout_test)
add_executable(operator_edit_test tests/ym2612/operator_edit_test.cpp)
target_include_directories(operator_edit_test PRIVATE src)
target_link_libraries(operator_edit_test PRIVATE megatoy_core)
add_test(NAME operator_edit_test COMMAND operator_edit_test)
add_executable(operator_selection_test tests/gui/operator_selection_test.cpp
  src/gui/components/operator_selection.cpp)
target_include_directories(operator_selection_test PRIVATE src)
add_test(NAME operator_selection_test COMMAND operator_selection_test)
add_executable(status_test tests/core/status_test.cpp src/core/status.cpp)
target_include_directories(status_test PRIVATE src)
add_test(NAME status_test COMMAND status_test)
add_executable(frame_scheduler_test tests/platform/frame_scheduler_test.cpp)
target_include_directories(frame_scheduler_test PRIVATE src)
add_test(NAME frame_scheduler_test COMMAND frame_scheduler_test)
add_executable(import_pipeline_test tests/platform/import_pipeline_test.cpp)
target_include_directories(import_pipeline_test PRIVATE src)
target_link_libraries(import_pipeline_test PRIVATE megatoy_core)
add_test(NAME import_pipeline_test COMMAND import_pipeline_test)
add_executable(persistent_parse_cache_test
  tests/patches/persistent_parse_cache_test.cpp)
target_include_directories(persistent_parse_cache_test PRIVATE src)
target_link_libraries(persistent_parse_cache_test PRIVATE megatoy_core)
add_test(NAME persistent_parse_cache_test COMMAND persistent_parse_cache_test)
add_executable(background_folder_scan_test
  tests/patches/background_folder_scan_test.cpp)
target_include_directories(background_folder_scan_test PRIVATE src)
target_link_libraries(background_folder_scan_test PRIVATE megatoy_core)
add_test(NAME background_folder_scan_test COMMAND background_folder_scan_test)
add_executable(version_test tests/update/version_test.cpp src/update/version.cpp)
target_include_directories(version_test PRIVATE src)
add_test(NAME version_test COMMAND version_test)
add_executable(workspace_test tests/workspace/workspace_test.cpp)
target_include_directories(workspace_test PRIVATE src)
target_link_libraries(workspace_test PRIVATE megatoy_core)
add_test(NAME workspace_test COMMAND workspace_test)

# Convenience target to build and run tests
add_custom_target(check
  DEPENDS patch_registry_test patch_io_roundtrip_test random_utils_test
          subsystem_tests ym2612_render_test analyzer_test command_queue_test
          performance_test
          ginpkg_history_test patch_write_test status_test
          filename_utils_test utf8_utils_test
          patch_repository_delete_test patch_repository_rename_test
          folder_metadata_test patch_tree_flatten_test
          workspace_test vgm_multi_instrument_test frame_scheduler_test
          version_test import_pipeline_test persistent_parse_cache_test
          background_folder_scan_test
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Copy license bundle during installation so release artifacts automatically
# contain the required notices alongside the binaries.
if(EXISTS "${CMAKE_SOURCE_DIR}/licenses")
  install(DIRECTORY "${CMAKE_SOURCE_DIR}/licenses"
          DESTINATION ".")
endif()
