cmake_minimum_required(VERSION 3.24)

set(MEGATOY_VERSION "0.3.8" CACHE STRING "Megatoy application version")
project(megatoy VERSION ${MEGATOY_VERSION})

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
        "-ffast-math"
        "-funroll-loops"
    )
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
    list(APPEND OPTIMIZATION_FLAGS ${MEGATOY_RELEASE_CPU_FLAGS})
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



include(cmake/targets.cmake)
