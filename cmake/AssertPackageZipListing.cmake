if(NOT DEFINED PACKAGE_PATH)
    message(FATAL_ERROR "PACKAGE_PATH is required")
endif()

if(NOT DEFINED EXPECTED_ENTRIES)
    message(FATAL_ERROR "EXPECTED_ENTRIES is required")
endif()

if(DEFINED PACKAGE_LISTING_PATH)
    if(NOT EXISTS "${PACKAGE_LISTING_PATH}")
        message(FATAL_ERROR "expected package listing fixture does not exist: ${PACKAGE_LISTING_PATH}")
    endif()
    file(READ "${PACKAGE_LISTING_PATH}" package_listing)
else()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-E" "tar" "tf" "${PACKAGE_PATH}"
        RESULT_VARIABLE list_result
        OUTPUT_VARIABLE package_listing
        ERROR_VARIABLE list_stderr
    )

    if(NOT list_result EQUAL 0)
        message(FATAL_ERROR
            "failed to list package ZIP ${PACKAGE_PATH} with exit code ${list_result}\nstderr:\n${list_stderr}")
    endif()
endif()

string(REPLACE "\r\n" "\n" package_listing "${package_listing}")
string(REPLACE "\r" "\n" package_listing "${package_listing}")
string(REGEX REPLACE "[\n]+$" "" package_listing "${package_listing}")
if(package_listing STREQUAL "")
    message(FATAL_ERROR "package ZIP listing is empty: ${PACKAGE_PATH}")
endif()
string(REPLACE "\n" ";" PACKAGE_ENTRIES "${package_listing}")

get_filename_component(package_filename "${PACKAGE_PATH}" NAME)
if(NOT package_filename MATCHES "\\.zip$")
    message(FATAL_ERROR "package path must name a .zip archive: ${PACKAGE_PATH}")
endif()
string(REGEX REPLACE "\\.zip$" "" PACKAGE_ROOT "${package_filename}")

set(seen_expected_entries)
foreach(expected_entry IN LISTS EXPECTED_ENTRIES)
    if(expected_entry STREQUAL "")
        message(FATAL_ERROR "expected package entry is empty: ${PACKAGE_PATH}")
    endif()

    if(expected_entry IN_LIST seen_expected_entries)
        message(FATAL_ERROR "expected package entries contain a duplicate entry: ${expected_entry}")
    endif()
    list(APPEND seen_expected_entries "${expected_entry}")

    string(FIND "${expected_entry}" "\\" expected_backslash_index)
    if(NOT expected_backslash_index EQUAL -1)
        message(FATAL_ERROR "expected package entry uses a backslash path separator: ${expected_entry}")
    endif()

    if(expected_entry MATCHES "^/")
        message(FATAL_ERROR "expected package entry uses an absolute path: ${expected_entry}")
    endif()

    if(expected_entry MATCHES "^[A-Za-z]:")
        message(FATAL_ERROR "expected package entry uses a drive-letter absolute path: ${expected_entry}")
    endif()

    if(expected_entry MATCHES "//")
        message(FATAL_ERROR "expected package entry contains an empty path component: ${expected_entry}")
    endif()

    if(expected_entry MATCHES "(^|/)\\.(/|$)")
        message(FATAL_ERROR "expected package entry contains a '.' path component: ${expected_entry}")
    endif()

    if(expected_entry MATCHES "(^|/)\\.\\.(/|$)")
        message(FATAL_ERROR "expected package entry contains a '..' path component: ${expected_entry}")
    endif()

    if("${expected_entry}" MATCHES "/$")
        message(FATAL_ERROR "expected package entry must name a file, not a directory: ${expected_entry}")
    endif()

    set(expected_entry_under_package_root FALSE)
    string(FIND "${expected_entry}" "${PACKAGE_ROOT}/" expected_package_root_prefix_index)
    if(expected_package_root_prefix_index EQUAL 0)
        set(expected_entry_under_package_root TRUE)
    endif()

    if(NOT expected_entry_under_package_root)
        message(FATAL_ERROR
            "expected package entry is outside the expected package root '${PACKAGE_ROOT}': ${expected_entry}")
    endif()
endforeach()

set(expected_directory_entries)
foreach(expected_entry IN LISTS EXPECTED_ENTRIES)
    set(current_path "${expected_entry}")
    while(TRUE)
        string(REGEX REPLACE "/[^/]+$" "" parent_path "${current_path}")
        if(parent_path STREQUAL current_path)
            break()
        endif()

        list(APPEND expected_directory_entries "${parent_path}/")
        set(current_path "${parent_path}")
    endwhile()
endforeach()

set(seen_package_entries)
foreach(package_entry IN LISTS PACKAGE_ENTRIES)
    if(package_entry STREQUAL "")
        message(FATAL_ERROR "package ZIP contains an empty entry name: ${PACKAGE_PATH}")
    endif()

    if(package_entry IN_LIST seen_package_entries)
        message(FATAL_ERROR "package ZIP contains duplicate entry: ${package_entry}")
    endif()
    list(APPEND seen_package_entries "${package_entry}")

    string(FIND "${package_entry}" "\\" backslash_index)
    if(NOT backslash_index EQUAL -1)
        message(FATAL_ERROR "package ZIP entry uses a backslash path separator: ${package_entry}")
    endif()

    if(package_entry MATCHES "^/")
        message(FATAL_ERROR "package ZIP entry uses an absolute path: ${package_entry}")
    endif()

    if(package_entry MATCHES "^[A-Za-z]:")
        message(FATAL_ERROR "package ZIP entry uses a drive-letter absolute path: ${package_entry}")
    endif()

    if(package_entry MATCHES "//")
        message(FATAL_ERROR "package ZIP entry contains an empty path component: ${package_entry}")
    endif()

    if(package_entry MATCHES "(^|/)\\.(/|$)")
        message(FATAL_ERROR "package ZIP entry contains a '.' path component: ${package_entry}")
    endif()

    if(package_entry MATCHES "(^|/)\\.\\.(/|$)")
        message(FATAL_ERROR "package ZIP entry contains a '..' path component: ${package_entry}")
    endif()

    if("${package_entry}" STREQUAL "${PACKAGE_ROOT}")
        message(FATAL_ERROR
            "package ZIP entry is a bare package root without a trailing slash: ${package_entry}")
    endif()

    set(entry_under_package_root FALSE)
    string(FIND "${package_entry}" "${PACKAGE_ROOT}/" package_root_prefix_index)
    if(package_root_prefix_index EQUAL 0)
        set(entry_under_package_root TRUE)
    endif()

    if(NOT entry_under_package_root)
        message(FATAL_ERROR
            "package ZIP entry is outside the expected package root '${PACKAGE_ROOT}': ${package_entry}")
    endif()

    set(package_entry_is_directory FALSE)
    if("${package_entry}" MATCHES "/$")
        set(package_entry_is_directory TRUE)
    endif()

    if(package_entry_is_directory)
        if(NOT "${package_entry}" STREQUAL "${PACKAGE_ROOT}")
            if(NOT package_entry IN_LIST expected_directory_entries)
                message(FATAL_ERROR "package ZIP contains unexpected directory entry: ${package_entry}")
            endif()
        endif()
    else()
        if(NOT package_entry IN_LIST EXPECTED_ENTRIES)
            message(FATAL_ERROR "package ZIP contains unexpected file entry: ${package_entry}")
        endif()
    endif()
endforeach()

foreach(expected_entry IN LISTS EXPECTED_ENTRIES)
    if(NOT expected_entry IN_LIST PACKAGE_ENTRIES)
        message(FATAL_ERROR "expected package entry missing: ${expected_entry}")
    endif()
endforeach()
