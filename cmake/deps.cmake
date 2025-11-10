include_guard(GLOBAL)

include(FetchContent)

find_package(OpenGL REQUIRED)
find_package(CURL REQUIRED)

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
set(NFD_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nfd)

# SDL3
FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL
  GIT_TAG        release-3.2.4
)
FetchContent_MakeAvailable(SDL3)

# Dear ImGui
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui
  GIT_TAG        docking
)
FetchContent_MakeAvailable(imgui)

# libvgm
FetchContent_Declare(
  libvgm
  GIT_REPOSITORY https://github.com/ValleyBell/libvgm
  GIT_TAG        e9f2b023e8918b56be0d2e634b3f5aab2a589ffe
)
if(WIN32)
  set(LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
  set(BUILD_PLAYER OFF CACHE BOOL "" FORCE)
  set(BUILD_VGM2WAV OFF CACHE BOOL "" FORCE)
  set(LINK_STATIC_LIBS ON CACHE BOOL "" FORCE)
  set(USE_SANITIZERS OFF CACHE BOOL "" FORCE)
  set(UTIL_CHARCNV_ICONV OFF CACHE BOOL "" FORCE)
  set(UTIL_CHARCNV_WINAPI ON CACHE BOOL "" FORCE)
  set(_LIBVGM_FETCH_ROOT "${CMAKE_BINARY_DIR}/_deps/libvgm-src")
  set(ZLIB_ROOT "${_LIBVGM_FETCH_ROOT}/libs" CACHE PATH "" FORCE)
  set(ZLIB_INCLUDE_DIR "${_LIBVGM_FETCH_ROOT}/libs/include" CACHE PATH "" FORCE)
  set(ZLIB_INCLUDE_DIRS "${_LIBVGM_FETCH_ROOT}/libs/include" CACHE PATH "" FORCE)
  set(ZLIB_LIBRARY "${_LIBVGM_FETCH_ROOT}/libs/lib/zlib64.lib" CACHE FILEPATH "" FORCE)
  set(ZLIB_LIBRARIES "${_LIBVGM_FETCH_ROOT}/libs/lib/zlib64.lib" CACHE FILEPATH "" FORCE)
  set(ZLIB_LIBRARY_RELEASE "${_LIBVGM_FETCH_ROOT}/libs/lib/zlib64.lib" CACHE FILEPATH "" FORCE)
  set(ZLIB_LIBRARY_DEBUG "${_LIBVGM_FETCH_ROOT}/libs/lib/zlib64d.lib" CACHE FILEPATH "" FORCE)
endif()
set(BUILD_LIBAUDIO OFF CACHE BOOL "" FORCE)
set(BUILD_PLAYER OFF CACHE BOOL "" FORCE)
set(BUILD_VGM2WAV OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(USE_SANITIZERS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libvgm)

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
set(KISSFFT_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(KISSFFT_TEST     OFF CACHE BOOL "" FORCE)
set(KISSFFT_TOOLS    OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(kissfft)

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

# FontAwesome Headers
FetchContent_Declare(
  IconFontCppHeaders
  GIT_REPOSITORY https://github.com/juliettef/IconFontCppHeaders.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(IconFontCppHeaders)
