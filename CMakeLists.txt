cmake_minimum_required(VERSION 3.16)
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
    set(OPTIMIZATION_FLAGS
        "-O3"
        "-DNDEBUG"
        "-ffast-math"
        "-funroll-loops"
    )
    list(APPEND OPTIMIZATION_FLAGS
        "-march=native"
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
