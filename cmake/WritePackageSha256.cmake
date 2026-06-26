if(NOT DEFINED PACKAGE_PATH)
    message(FATAL_ERROR "PACKAGE_PATH is required")
endif()

if(NOT DEFINED OUTPUT_PATH)
    message(FATAL_ERROR "OUTPUT_PATH is required")
endif()

get_filename_component(package_name "${PACKAGE_PATH}" NAME)
if(NOT package_name MATCHES "\\.zip$")
    message(FATAL_ERROR "package checksum writer package path must name a .zip archive: ${PACKAGE_PATH}")
endif()

get_filename_component(expected_output_path "${PACKAGE_PATH}.sha256" ABSOLUTE)
get_filename_component(actual_output_path "${OUTPUT_PATH}" ABSOLUTE)
if(NOT actual_output_path STREQUAL expected_output_path)
    message(FATAL_ERROR
        "package checksum writer output path must be the package path plus .sha256\nexpected: ${expected_output_path}\nactual: ${actual_output_path}")
endif()

if(NOT EXISTS "${PACKAGE_PATH}")
    message(FATAL_ERROR "package ZIP does not exist: ${PACKAGE_PATH}")
endif()

file(SIZE "${PACKAGE_PATH}" package_size)
if(package_size LESS 1)
    message(FATAL_ERROR "package ZIP is empty: ${PACKAGE_PATH}")
endif()

file(SHA256 "${PACKAGE_PATH}" package_sha256)

file(WRITE "${OUTPUT_PATH}" "${package_sha256}  ${package_name}\n")
message(STATUS "wrote ${OUTPUT_PATH}: ${package_sha256}  ${package_name}")
