cmake_minimum_required(VERSION 3.24)
project(megatoy)

enable_testing()

# Set Release as the default build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Compiler-specific optimization settings
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  message(STATUS "Setting up Release build optimizations")
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
      option(MEGATOY_GENERAL_X86_64_LINUX "Build with generic x86-64 flags on Linux" OFF)
    endif()
    set(OPTIMIZATION_FLAGS
        "-O3"
        "-DNDEBUG"
        "-ffast-math"
        "-funroll-loops"
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
      if(MEGATOY_GENERAL_X86_64_LINUX)
        message(STATUS "Using generic x86-64 tuning for Linux Release build")
        list(APPEND OPTIMIZATION_FLAGS
            "-march=x86-64"
            "-mtune=generic"
        )
      else()
        message(STATUS "Using native CPU optimizations for Linux Release build")
        list(APPEND OPTIMIZATION_FLAGS "-march=native")
      endif()
    else()
      list(APPEND OPTIMIZATION_FLAGS "-march=native")
    endif()
    list(APPEND OPTIMIZATION_FLAGS
        "-flto"
        "-fomit-frame-pointer"
    )
    string(JOIN " " CMAKE_CXX_FLAGS_RELEASE ${OPTIMIZATION_FLAGS})
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
