if(NOT DEFINED PACKAGE_PATH)
    message(FATAL_ERROR "PACKAGE_PATH is required")
endif()

if(NOT DEFINED CHECKSUM_PATH)
    message(FATAL_ERROR "CHECKSUM_PATH is required")
endif()

get_filename_component(package_name "${PACKAGE_PATH}" NAME)
if(NOT package_name MATCHES "\\.zip$")
    message(FATAL_ERROR "package checksum package path must name a .zip archive: ${PACKAGE_PATH}")
endif()

if(NOT EXISTS "${PACKAGE_PATH}")
    message(FATAL_ERROR "expected package ZIP does not exist: ${PACKAGE_PATH}")
endif()

file(SIZE "${PACKAGE_PATH}" package_size)
if(package_size LESS 1)
    message(FATAL_ERROR "expected package ZIP to be non-empty: ${PACKAGE_PATH}")
endif()

if(NOT EXISTS "${CHECKSUM_PATH}")
    message(FATAL_ERROR "expected package checksum does not exist: ${CHECKSUM_PATH}")
endif()

get_filename_component(expected_checksum_path "${PACKAGE_PATH}.sha256" ABSOLUTE)
get_filename_component(actual_checksum_path "${CHECKSUM_PATH}" ABSOLUTE)
if(NOT actual_checksum_path STREQUAL expected_checksum_path)
    message(FATAL_ERROR
        "package checksum path must be the package path plus .sha256\nexpected: ${expected_checksum_path}\nactual: ${actual_checksum_path}")
endif()

file(SIZE "${CHECKSUM_PATH}" checksum_size)
if(checksum_size LESS 1)
    message(FATAL_ERROR "expected package checksum to be non-empty: ${CHECKSUM_PATH}")
endif()

file(SHA256 "${PACKAGE_PATH}" expected_sha256)
set(expected_line "${expected_sha256}  ${package_name}")

file(READ "${CHECKSUM_PATH}" actual_text)
string(REPLACE "\r\n" "\n" actual_text "${actual_text}")
string(REPLACE "\r" "\n" actual_text "${actual_text}")
set(expected_text "${expected_line}\n")

if(NOT actual_text STREQUAL expected_text)
    message(FATAL_ERROR
        "package checksum mismatch\nexpected: ${expected_text}actual: ${actual_text}")
endif()
