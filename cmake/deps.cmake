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
    GIT_TAG        v1.3.0
  )
  set(NFD_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(nfd)
endif()

# SDL3
FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL
  GIT_TAG        release-3.4.12
)
FetchContent_MakeAvailable(SDL3)

# Dear ImGui
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui
  # The docking feature lives on its own branch; -docking tags are its
  # releases. Pinned rather than tracking the branch so a build is
  # reproducible and an upstream change cannot break CI unannounced.
  GIT_TAG        v1.92.9-docking
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

  # SQLiteCpp is kept native-only for the one-time import of metadata written
  # by pre-workspace releases. The browser never created that database.
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

# stb single-header image loader
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  # stb publishes no releases; pinned to a known-good commit.
  GIT_TAG        31c1ad37456438565541f4919958214b6e762fb4
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
  GIT_TAG        131.2.0
)
set(KISSFFT_STATIC ON CACHE BOOL "" FORCE)
set(KISSFFT_SHARED OFF CACHE BOOL "" FORCE)
set(KISSFFT_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(KISSFFT_TEST     OFF CACHE BOOL "" FORCE)
set(KISSFFT_TOOLS    OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(kissfft)

# FontAwesome Headers
FetchContent_Declare(
  IconFontCppHeaders
  GIT_REPOSITORY https://github.com/juliettef/IconFontCppHeaders.git
  # No releases upstream; pinned to a known-good commit.
  GIT_TAG        210b5a399a64270674560d633638952d1e8d804d
)
FetchContent_MakeAvailable(IconFontCppHeaders)

# ym2612_format - patch file readers and writers.
# It also brings in miniz, which megatoy uses directly for .ginpkg packaging.
FetchContent_Declare(
  ym2612_format
  GIT_REPOSITORY https://github.com/ulalume/ym2612_format
  GIT_TAG        v0.2.4
)
set(YM2612_FORMAT_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(YM2612_FORMAT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ym2612_format)

# ym2612-patches - the built-in CC0 preset library. Data only (LICENSE,
# .gitignore, *.dmp files); it ships no CMakeLists.txt, so
# FetchContent_MakeAvailable just populates ym2612_patches_SOURCE_DIR instead
# of add_subdirectory()-ing it. The repo publishes no tags, so the pin below
# is a commit SHA -- bumping that SHA is the entire follow-upstream
# procedure. For offline/local development, override with
# FETCHCONTENT_SOURCE_DIR_YM2612_PATCHES (dev-only, never committed; same
# policy as the other dependency overrides).
FetchContent_Declare(
  ym2612_patches
  GIT_REPOSITORY https://github.com/ulalume/ym2612-patches.git
  GIT_TAG        60c011ce5c2cb98426ef351089c4802af2cc1441
)
FetchContent_MakeAvailable(ym2612_patches)
