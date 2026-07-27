include_guard(GLOBAL)

include(FetchContent)

if(NOT EMSCRIPTEN)
  find_package(OpenGL REQUIRED)
  find_package(CURL REQUIRED)
endif()

# nlohmann_json
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json
  GIT_TAG        v3.12.0
)
FetchContent_MakeAvailable(nlohmann_json)

if(NOT EMSCRIPTEN)
  # nativefiledialog-extended
  FetchContent_Declare(
    nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended
    GIT_TAG        v1.2.1
  )
  set(NFD_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(nfd)
endif()

# SDL3
FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL
  GIT_TAG        release-3.2.26
)
FetchContent_MakeAvailable(SDL3)

# Dear ImGui
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui
  GIT_TAG        docking
)
FetchContent_MakeAvailable(imgui)

# ymfm - YM2612 emulation core.
# ymfm ships no CMake project, so only the sources are fetched; the library
# target is defined in cmake/targets.cmake.
FetchContent_Declare(
  ymfm
  GIT_REPOSITORY https://github.com/aaronsgiles/ymfm
  GIT_TAG        17decfae857b92ab55fbb30ade2287ace095a381
)
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_GetProperties(ymfm)
if(NOT ymfm_POPULATED)
  FetchContent_Populate(ymfm)
endif()

if(NOT EMSCRIPTEN)
  # RtMidi
  FetchContent_Declare(
    rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi
    GIT_TAG        6.0.0
  )
  set(RTMIDI_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
  set(RTMIDI_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(RTMIDI_TARGETNAME_UNINSTALL rtmidi_uninstall CACHE STRING "" FORCE)
  FetchContent_MakeAvailable(rtmidi)
endif()

# stb single-header image loader
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG        master
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)

# chord_detector
FetchContent_Declare(
  chord_detector
  GIT_REPOSITORY https://github.com/ulalume/chord_detector
  GIT_TAG        v0.0.2
)
FetchContent_MakeAvailable(chord_detector)

# kissfft
FetchContent_Declare(
  kissfft
  GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
  GIT_TAG        master
)
set(KISSFFT_STATIC ON CACHE BOOL "" FORCE)
set(KISSFFT_SHARED OFF CACHE BOOL "" FORCE)
set(KISSFFT_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(KISSFFT_TEST     OFF CACHE BOOL "" FORCE)
set(KISSFFT_TOOLS    OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(kissfft)

if(NOT EMSCRIPTEN)
  # SQLiteCpp
  FetchContent_Declare(
    SQLiteCpp
    GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
    GIT_TAG        3.3.1
  )
  set(SQLITECPP_INTERNAL_SQLITE ON CACHE BOOL "" FORCE)
  set(SQLITECPP_RUN_CPPLINT OFF CACHE BOOL "" FORCE)
  set(SQLITECPP_RUN_CPPCHECK OFF CACHE BOOL "" FORCE)
  set(SQLITECPP_RUN_DOXYGEN OFF CACHE BOOL "" FORCE)
  set(SQLITECPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SQLITECPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(SQLiteCpp)
endif()

# FontAwesome Headers
FetchContent_Declare(
  IconFontCppHeaders
  GIT_REPOSITORY https://github.com/juliettef/IconFontCppHeaders.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(IconFontCppHeaders)

FetchContent_Declare(
  miniz
  GIT_REPOSITORY https://github.com/richgel999/miniz
  GIT_TAG        2.2.0
)
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()
FetchContent_GetProperties(miniz)
if(NOT miniz_POPULATED)
  FetchContent_Populate(miniz)
  set(miniz_export_header "${miniz_SOURCE_DIR}/miniz_export.h")
  if(NOT EXISTS "${miniz_export_header}")
    file(WRITE "${miniz_export_header}" "#pragma once\n\n#if defined(_WIN32)\n#if defined(MINIZ_SHARED)\n#if defined(MINIZ_EXPORTS)\n#define MINIZ_EXPORT __declspec(dllexport)\n#else\n#define MINIZ_EXPORT __declspec(dllimport)\n#endif\n#else\n#define MINIZ_EXPORT\n#endif\n#elif defined(__GNUC__) && __GNUC__ >= 4\n#define MINIZ_EXPORT __attribute__((visibility(\"default\")))\n#else\n#define MINIZ_EXPORT\n#endif\n")
  endif()
endif()
add_library(miniz STATIC
  ${miniz_SOURCE_DIR}/miniz.c
  ${miniz_SOURCE_DIR}/miniz_tdef.c
  ${miniz_SOURCE_DIR}/miniz_tinfl.c
  ${miniz_SOURCE_DIR}/miniz_zip.c
)
target_include_directories(miniz PUBLIC ${miniz_SOURCE_DIR})
