if(NOT DEFINED src)
  message(FATAL_ERROR "update_compile_commands.cmake requires -Dsrc=<path>")
endif()
if(NOT DEFINED dst)
  message(FATAL_ERROR "update_compile_commands.cmake requires -Ddst=<path>")
endif()
if(NOT DEFINED mirror_command)
  message(FATAL_ERROR "update_compile_commands.cmake requires -Dmirror_command=<command>")
endif()

file(REMOVE "${dst}")

if(NOT EXISTS "${src}")
  # Nothing to mirror; leave the destination absent.
  return()
endif()

if(mirror_command STREQUAL "create_symlink")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E create_symlink "${src}" "${dst}"
    RESULT_VARIABLE _mirror_result
  )
elseif(mirror_command STREQUAL "copy_if_different")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${src}" "${dst}"
    RESULT_VARIABLE _mirror_result
  )
else()
  message(FATAL_ERROR
    "Unknown mirror_command '${mirror_command}' passed to update_compile_commands.cmake")
endif()

if(NOT _mirror_result EQUAL 0)
  message(FATAL_ERROR
    "Failed to mirror compile_commands.json with command '${mirror_command}' "
    "(${_mirror_result})")
endif()
