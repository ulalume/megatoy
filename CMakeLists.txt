cmake_minimum_required(VERSION 3.16)
project(vgm-test)
include(FetchContent)

# Set Release as the default build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Compiler-specific optimization settings
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  message(STATUS "Setting up Release build optimizations")
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Baseline optimization flags
    set(OPTIMIZATION_FLAGS
            "-O3"              # Highest level optimization
            "-DNDEBUG"         # Disable asserts
            "-ffast-math"      # Fast math operations
            "-funroll-loops"   # Loop unrolling
        )

    # Additional optimization flags (optional)
    list(APPEND OPTIMIZATION_FLAGS
            "-march=native"    # Optimize for the host CPU
            "-flto"           # Link Time Optimization
            "-fomit-frame-pointer"  # Omit the frame pointer
        )

    # Join the flags into a single string
    string(JOIN " " CMAKE_CXX_FLAGS_RELEASE ${OPTIMIZATION_FLAGS})

    # Apply LTO to the linker as well
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto")

  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # Optimizations for MSVC
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG /fp:fast /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG")  # Link Time Code Generation
  endif()

  message(STATUS "Release CXX flags: ${CMAKE_CXX_FLAGS_RELEASE}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find OpenGL
find_package(OpenGL REQUIRED)

# nlohmann_json
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json
  GIT_TAG        v3.12.0
)
FetchContent_MakeAvailable(nlohmann_json)

# nativefiledialog-extended
FetchContent_Declare(
  nfd
  GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended
  GIT_TAG        v1.2.1
)
FetchContent_MakeAvailable(nfd)

# GLFW
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw
  GIT_TAG        3.4
)
FetchContent_MakeAvailable(glfw)

# Dear ImGui
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui
  GIT_TAG        v1.92.3
)
FetchContent_MakeAvailable(imgui)

# libvgm
FetchContent_Declare(
  libvgm
  GIT_REPOSITORY https://github.com/ValleyBell/libvgm
  GIT_TAG        e9f2b023e8918b56be0d2e634b3f5aab2a589ffe
)
FetchContent_MakeAvailable(libvgm)

# stb single-header image loader
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG        master
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)

# Create Dear ImGui library
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
  src/preference_manager.cpp
  src/channel_allocator.cpp
  src/resource_manager.cpp
  src/patches/patch_repository.cpp
  src/platform/file_dialog.cpp
  src/parsers/ctrmml_parser.cpp
  src/parsers/fui_parser.cpp
  src/parsers/rym2612_parser.cpp
  src/parsers/dmp_parser.cpp
  src/parsers/util.cpp
  src/ui/keyboard_typing.cpp
  src/ui/preview/preview.cpp
  src/ui/preview/algorithm_preview.cpp
  src/ui/preview/ssg_preview.cpp
  src/ui/main_menu.cpp
  src/ui/patch_editor.cpp
  src/ui/operator_editor.cpp
  src/ui/midi_keyboard.cpp
  src/ui/patch_selector.cpp
  src/ui/preferences.cpp
  src/ym2612/device.cpp
  src/ym2612/channel.cpp
  src/ym2612/operator.cpp
  src/ym2612/patch_io.cpp
)

if(APPLE)
  add_executable(megatoy MACOSX_BUNDLE ${MEGATOY_SOURCES})
elseif(WIN32)
  add_executable(megatoy WIN32 ${MEGATOY_SOURCES})
else()
  add_executable(megatoy ${MEGATOY_SOURCES})
endif()
# Apply target-specific optimizations only in Release builds
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
)

target_include_directories(megatoy PRIVATE
  ${libvgm_SOURCE_DIR}
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${stb_SOURCE_DIR}
  ${CMAKE_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/src
)

target_compile_definitions(megatoy PRIVATE
  VGM_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
  USE_EMBEDDED_RESOURCES
)

# Resource embedding function
function(embed_resource resource_file output_file variable_name)
  file(READ ${resource_file} file_content HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," file_content "${file_content}")
  string(REGEX REPLACE ",$" "" file_content "${file_content}")
  file(WRITE ${output_file}
        "#pragma once\n"
        "#include <cstdint>\n"
        "#include <cstddef>\n\n"
        "namespace embedded_assets {\n"
        "inline const uint8_t ${variable_name}[] = {\n"
        "${file_content}\n"
        "};\n"
        "inline const size_t ${variable_name}_size = sizeof(${variable_name});\n"
        "}\n"
    )
endfunction()

# Generate embedded assets
set(ASSET_FILES
    algorithm0.png
    algorithm1.png
    algorithm2.png
    algorithm3.png
    algorithm4.png
    algorithm5.png
    algorithm6.png
    algorithm7.png
    ssg0.png
    ssg1.png
    ssg2.png
    ssg3.png
    ssg4.png
    ssg5.png
    ssg6.png
    ssg7.png
    ssgoff.png
)

set(EMBEDDED_HEADERS)
foreach(asset ${ASSET_FILES})
  string(REPLACE "." "_" var_name ${asset})
  set(output_file "${CMAKE_BINARY_DIR}/embedded_${var_name}.hpp")
  embed_resource("${CMAKE_SOURCE_DIR}/assets/${asset}" "${output_file}" "${var_name}")
  list(APPEND EMBEDDED_HEADERS "${output_file}")
endforeach()

# Generate embedded assets registry
set(REGISTRY_FILE "${CMAKE_BINARY_DIR}/embedded_assets_registry.hpp")
file(WRITE ${REGISTRY_FILE}
    "#pragma once\n"
    "#include <unordered_map>\n"
    "#include <string>\n"
    "#include <cstdint>\n"
    "#include <cstddef>\n\n"
)

foreach(asset ${ASSET_FILES})
  string(REPLACE "." "_" var_name ${asset})
  file(APPEND ${REGISTRY_FILE} "#include \"embedded_${var_name}.hpp\"\n")
endforeach()

file(APPEND ${REGISTRY_FILE}
    "\nnamespace embedded_assets {\n"
    "struct ResourceInfo {\n"
    "    const uint8_t* data;\n"
    "    size_t size;\n"
    "};\n\n"
    "inline const std::unordered_map<std::string, ResourceInfo> resource_registry = {\n"
)

foreach(asset ${ASSET_FILES})
  string(REPLACE "." "_" var_name ${asset})
  file(APPEND ${REGISTRY_FILE} "    {\"${asset}\", {${var_name}, ${var_name}_size}},\n")
endforeach()

file(APPEND ${REGISTRY_FILE}
    "};\n"
    "}\n"
)

# Add embedded headers as dependency
add_custom_target(generate_embedded_assets DEPENDS ${EMBEDDED_HEADERS} ${REGISTRY_FILE})
add_dependencies(megatoy generate_embedded_assets)

target_include_directories(megatoy PRIVATE
  ${CMAKE_BINARY_DIR}
)

add_custom_command(
    TARGET megatoy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove -f
            ${CMAKE_SOURCE_DIR}/compile_commands.json
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            ${CMAKE_BINARY_DIR}/compile_commands.json
            ${CMAKE_SOURCE_DIR}/compile_commands.json
)
