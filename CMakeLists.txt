cmake_minimum_required(VERSION 3.24)

set(MEGATOY_VERSION "0.5.6" CACHE STRING "megatoy application version")
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
    if(NOT EMSCRIPTEN)
      if(MEGATOY_LINUX_X86_64)
        if(MEGATOY_GENERAL_X86_64_LINUX)
          message(STATUS "Using generic x86-64 tuning for Linux Release build")
          set(MEGATOY_RELEASE_CPU_FLAGS "-march=x86-64" "-mtune=generic")
        else()
          message(STATUS "Using native CPU optimizations for Linux Release build")
          set(MEGATOY_RELEASE_CPU_FLAGS "-march=native")
        endif()
      else()
        set(MEGATOY_RELEASE_CPU_FLAGS "-march=native")
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

include(cmake/deps.cmake)
include(cmake/embed_assets.cmake)

if(EMSCRIPTEN)
  if(NOT MEGATOY_EMSCRIPTEN_SYSROOT AND DEFINED ENV{EM_CACHE})
    set(MEGATOY_EMSCRIPTEN_SYSROOT "$ENV{EM_CACHE}/sysroot")
  endif()
  if(NOT TARGET ZLIB::ZLIB AND MEGATOY_EMSCRIPTEN_SYSROOT)
    add_library(ZLIB::ZLIB UNKNOWN IMPORTED)
    set_target_properties(ZLIB::ZLIB PROPERTIES
      IMPORTED_LOCATION "${MEGATOY_EMSCRIPTEN_SYSROOT}/lib/wasm32-emscripten/libz.a"
    )
  endif()
  if(TARGET ZLIB::ZLIB)
    # Prevent the Emscripten zlib port from injecting the sysroot include path ahead of libc++ headers.
    set_target_properties(ZLIB::ZLIB PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES ""
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES ""
    )
  endif()

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

# Copy license bundle during installation so release artifacts automatically
# contain the required notices alongside the binaries.
if(EXISTS "${CMAKE_SOURCE_DIR}/licenses")
  install(DIRECTORY "${CMAKE_SOURCE_DIR}/licenses"
          DESTINATION ".")
endif()
