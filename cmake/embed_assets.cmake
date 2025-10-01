include_guard(GLOBAL)

function(megatoy_embed_resource resource_file output_file variable_name)
  file(READ ${resource_file} file_content HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," file_content "${file_content}")
  string(REGEX REPLACE ",$" "" file_content "${file_content}")
  file(WRITE ${output_file}
        "#pragma once\n"
        "#include <cstdint>\n"
        "#include <cstddef>\n\n"
        "namespace embedded_assets {\n"
        "inline const uint8_t ${variable_name}[] = {\n"
        "${file_content}\n"
        "};\n"
        "inline const size_t ${variable_name}_size = sizeof(${variable_name});\n"
        "}\n"
  )
endfunction()

function(megatoy_sanitize_identifier input output_var)
  string(REGEX REPLACE "[^A-Za-z0-9]" "_" identifier "${input}")
  string(REGEX REPLACE "_+" "_" identifier "${identifier}")
  set(${output_var} "asset_${identifier}" PARENT_SCOPE)
endfunction()

function(add_embedded_assets target)
  set(options)
  set(oneValueArgs ASSET_DIR OUTPUT_DIR TARGET_NAME REGISTRY_FILE)
  set(multiValueArgs EXCLUDE_PATTERNS)
  cmake_parse_arguments(EMBED "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT TARGET ${target})
    message(FATAL_ERROR "add_embedded_assets: target '${target}' does not exist")
  endif()

  if(NOT EMBED_ASSET_DIR)
    set(EMBED_ASSET_DIR "${CMAKE_SOURCE_DIR}/assets")
  endif()

  if(NOT EXISTS "${EMBED_ASSET_DIR}")
    message(FATAL_ERROR "Asset directory '${EMBED_ASSET_DIR}' does not exist")
  endif()

  if(NOT EMBED_OUTPUT_DIR)
    set(EMBED_OUTPUT_DIR "${CMAKE_BINARY_DIR}/embedded_assets")
  endif()

  if(NOT EMBED_TARGET_NAME)
    set(EMBED_TARGET_NAME "generate_embedded_assets")
  endif()

  if(NOT EMBED_REGISTRY_FILE)
    set(EMBED_REGISTRY_FILE "${CMAKE_BINARY_DIR}/embedded_assets_registry.hpp")
  endif()

  file(MAKE_DIRECTORY "${EMBED_OUTPUT_DIR}")

  file(GLOB_RECURSE EMBEDDABLE_ASSET_FILES
       CONFIGURE_DEPENDS
       LIST_DIRECTORIES FALSE
       RELATIVE "${EMBED_ASSET_DIR}"
       "${EMBED_ASSET_DIR}/*")

  list(SORT EMBEDDABLE_ASSET_FILES)

  if(EMBED_EXCLUDE_PATTERNS)
    foreach(pattern ${EMBED_EXCLUDE_PATTERNS})
      list(FILTER EMBEDDABLE_ASSET_FILES EXCLUDE REGEX "${pattern}")
    endforeach()
  endif()

  set(EMBEDDED_HEADERS)
  set(EMBEDDED_REGISTRY_INCLUDES)
  set(EMBEDDED_REGISTRY_ENTRIES)

  foreach(relative_path ${EMBEDDABLE_ASSET_FILES})
    file(TO_CMAKE_PATH "${relative_path}" cmake_style_relative_path)

    megatoy_sanitize_identifier("${cmake_style_relative_path}" identifier)
    set(output_file "${EMBED_OUTPUT_DIR}/${identifier}.hpp")

    megatoy_embed_resource("${EMBED_ASSET_DIR}/${cmake_style_relative_path}"
                           "${output_file}" "${identifier}")

    list(APPEND EMBEDDED_HEADERS "${output_file}")
    list(APPEND EMBEDDED_REGISTRY_INCLUDES
         "#include \"embedded_assets/${identifier}.hpp\"")
    list(APPEND EMBEDDED_REGISTRY_ENTRIES
         "    {\"${cmake_style_relative_path}\", {${identifier}, ${identifier}_size}},")
  endforeach()

  list(REMOVE_DUPLICATES EMBEDDED_REGISTRY_INCLUDES)
  list(SORT EMBEDDED_REGISTRY_INCLUDES)
  list(SORT EMBEDDED_REGISTRY_ENTRIES)

  file(WRITE ${EMBED_REGISTRY_FILE}
      "#pragma once\n"
      "#include <unordered_map>\n"
      "#include <string>\n"
      "#include <cstdint>\n"
      "#include <cstddef>\n\n"
  )

  foreach(include_line ${EMBEDDED_REGISTRY_INCLUDES})
    file(APPEND ${EMBED_REGISTRY_FILE} "${include_line}\n")
  endforeach()

  file(APPEND ${EMBED_REGISTRY_FILE}
      "\nnamespace embedded_assets {\n"
      "struct ResourceInfo {\n"
      "    const uint8_t* data;\n"
      "    size_t size;\n"
      "};\n\n"
      "inline const std::unordered_map<std::string, ResourceInfo> resource_registry = {\n"
  )

  foreach(entry ${EMBEDDED_REGISTRY_ENTRIES})
    file(APPEND ${EMBED_REGISTRY_FILE} "${entry}\n")
  endforeach()

  file(APPEND ${EMBED_REGISTRY_FILE}
      "};\n"
      "}\n"
  )

  add_custom_target(${EMBED_TARGET_NAME}
                    DEPENDS ${EMBEDDED_HEADERS} ${EMBED_REGISTRY_FILE})
  add_dependencies(${target} ${EMBED_TARGET_NAME})
endfunction()
