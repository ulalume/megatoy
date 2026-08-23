# MEGATOY_PLATFORM_WEB and MEGATOY_PLATFORM_DESKTOP come from a header, so a
# file that forgets to include it compiles the wrong branch in silence.

set(config_header "platform/platform_config.hpp")
file(GLOB_RECURSE sources "${MEGATOY_SOURCE_DIR}/src/*.cpp"
                          "${MEGATOY_SOURCE_DIR}/src/*.hpp")

set(offenders "")
foreach(source IN LISTS sources)
  if(source MATCHES "${config_header}$")
    continue()
  endif()

  file(READ "${source}" text)
  if(NOT text MATCHES "MEGATOY_PLATFORM_(WEB|DESKTOP)")
    continue()
  endif()

  # A .cpp may rely on its own header for the include.
  set(candidates "${text}")
  string(REGEX REPLACE "\\.cpp$" ".hpp" paired "${source}")
  if(NOT paired STREQUAL source AND EXISTS "${paired}")
    file(READ "${paired}" paired_text)
    list(APPEND candidates "${paired_text}")
  endif()

  set(included FALSE)
  foreach(candidate IN LISTS candidates)
    if(candidate MATCHES "#include[ \t]*\"${config_header}\"")
      set(included TRUE)
    endif()
  endforeach()

  if(NOT included)
    file(RELATIVE_PATH relative "${MEGATOY_SOURCE_DIR}" "${source}")
    list(APPEND offenders "${relative}")
  endif()
endforeach()

if(offenders)
  string(REPLACE ";" "\n  " listing "${offenders}")
  message(FATAL_ERROR
          "Uses a platform macro without including ${config_header}:\n  ${listing}")
endif()
