# src/platform/web is built with the GUI, and logic may still include it there.
set(logic_directories core formats ym2612 audio patches workspace history
                      preferences midi system update platform)
set(ui_includes
    "gui/|imgui|app_context\\.hpp|app_state\\.hpp|app_services\\.hpp|patch_actions\\.hpp")

set(offenders "")
foreach(directory IN LISTS logic_directories)
  file(GLOB_RECURSE sources
       "${MEGATOY_SOURCE_DIR}/src/${directory}/*.cpp"
       "${MEGATOY_SOURCE_DIR}/src/${directory}/*.hpp"
       "${MEGATOY_SOURCE_DIR}/src/${directory}/*.c"
       "${MEGATOY_SOURCE_DIR}/src/${directory}/*.h"
       "${MEGATOY_SOURCE_DIR}/src/${directory}/*.mm")
  foreach(source IN LISTS sources)
    file(RELATIVE_PATH relative "${MEGATOY_SOURCE_DIR}" "${source}")
    if(relative MATCHES "^src/platform/web/")
      continue()
    endif()

    file(READ "${source}" text)
    if(text MATCHES
       "#[ \t]*include[ \t]*[<\"][^>\"\n]*(${ui_includes})")
      list(APPEND offenders "${relative}")
    endif()
  endforeach()
endforeach()

if(offenders)
  string(REPLACE ";" "\n  " listing "${offenders}")
  message(FATAL_ERROR "Logic includes a UI dependency:\n  ${listing}")
endif()
