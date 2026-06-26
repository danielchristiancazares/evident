if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

if(NOT DEFINED EXECUTABLE_NAME)
    message(FATAL_ERROR "EXECUTABLE_NAME is required")
endif()

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

if(NOT DEFINED INSTALL_BINDIR)
    message(FATAL_ERROR "INSTALL_BINDIR is required")
endif()

if(NOT DEFINED INSTALL_DOCDIR)
    message(FATAL_ERROR "INSTALL_DOCDIR is required")
endif()

if(NOT DEFINED SMOKE_INPUT_PATH)
    message(FATAL_ERROR "SMOKE_INPUT_PATH is required")
endif()

if(NOT DEFINED EXPECTED_VERSION_PATH)
    message(FATAL_ERROR "EXPECTED_VERSION_PATH is required")
endif()

if(NOT DEFINED EXPECTED_HELP_PATH)
    message(FATAL_ERROR "EXPECTED_HELP_PATH is required")
endif()

if(NOT DEFINED EXPECTED_TOOLCHAIN_PATH)
    message(FATAL_ERROR "EXPECTED_TOOLCHAIN_PATH is required")
endif()

if(NOT DEFINED EXPECTED_DOCS)
    message(FATAL_ERROR "EXPECTED_DOCS is required")
endif()

if(NOT DEFINED INSTALL_PREFIX)
    string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" install_suffix)
    set(INSTALL_PREFIX "${BUILD_DIR}/install-layout-${install_suffix}")
endif()

function(assert_compiler_stdout smoke_name compiler_path)
    execute_process(
        COMMAND "${compiler_path}" ${ARGN}
        RESULT_VARIABLE smoke_result
        OUTPUT_VARIABLE smoke_stdout
        ERROR_VARIABLE smoke_stderr
    )

    if(NOT smoke_result EQUAL 0)
        message(FATAL_ERROR
            "${smoke_name} failed with exit code ${smoke_result}\nstdout:\n${smoke_stdout}\nstderr:\n${smoke_stderr}")
    endif()

    if(NOT smoke_stderr STREQUAL "")
        message(FATAL_ERROR
            "${smoke_name} wrote unexpected stderr\nstdout:\n${smoke_stdout}\nstderr:\n${smoke_stderr}")
    endif()

    if(smoke_stdout STREQUAL "")
        message(FATAL_ERROR "${smoke_name} produced no stdout")
    endif()
endfunction()

function(normalize_text text out_var)
    set(normalized "${text}")
    string(REPLACE "\r\n" "\n" normalized "${normalized}")
    string(REPLACE "\r" "\n" normalized "${normalized}")
    string(REGEX REPLACE "[\n]+$" "\n" normalized "${normalized}")
    set("${out_var}" "${normalized}" PARENT_SCOPE)
endfunction()

function(assert_compiler_stdout_matches smoke_name compiler_path expected_path)
    execute_process(
        COMMAND "${compiler_path}" ${ARGN}
        RESULT_VARIABLE smoke_result
        OUTPUT_VARIABLE smoke_stdout
        ERROR_VARIABLE smoke_stderr
    )

    if(NOT smoke_result EQUAL 0)
        message(FATAL_ERROR
            "${smoke_name} failed with exit code ${smoke_result}\nstdout:\n${smoke_stdout}\nstderr:\n${smoke_stderr}")
    endif()

    if(NOT smoke_stderr STREQUAL "")
        message(FATAL_ERROR
            "${smoke_name} wrote unexpected stderr\nstdout:\n${smoke_stdout}\nstderr:\n${smoke_stderr}")
    endif()

    file(READ "${expected_path}" expected_stdout)
    normalize_text("${expected_stdout}" expected_stdout)
    normalize_text("${smoke_stdout}" smoke_stdout)

    if(NOT smoke_stdout STREQUAL expected_stdout)
        message(FATAL_ERROR
            "${smoke_name} stdout mismatch\nexpected:\n${expected_stdout}\nactual:\n${smoke_stdout}")
    endif()
endfunction()

function(assert_compiler_toolchain_check smoke_name compiler_path expected_prefix_path)
    execute_process(
        COMMAND "${compiler_path}" "--check-toolchain"
        RESULT_VARIABLE smoke_result
        OUTPUT_VARIABLE smoke_stdout
        ERROR_VARIABLE smoke_stderr
    )

    if(NOT smoke_result EQUAL 0)
        message(FATAL_ERROR
            "${smoke_name} failed with exit code ${smoke_result}\nstdout:\n${smoke_stdout}\nstderr:\n${smoke_stderr}")
    endif()

    if(NOT smoke_stderr STREQUAL "")
        message(FATAL_ERROR
            "${smoke_name} wrote unexpected stderr\nstdout:\n${smoke_stdout}\nstderr:\n${smoke_stderr}")
    endif()

    file(READ "${expected_prefix_path}" expected_prefix)
    normalize_text("${expected_prefix}" expected_prefix)
    normalize_text("${smoke_stdout}" smoke_stdout)

    string(LENGTH "${expected_prefix}" expected_prefix_length)
    string(SUBSTRING "${smoke_stdout}" 0 ${expected_prefix_length} actual_prefix)
    if(NOT actual_prefix STREQUAL expected_prefix)
        message(FATAL_ERROR
            "${smoke_name} stable stdout prefix mismatch\nexpected prefix:\n${expected_prefix}\nactual:\n${smoke_stdout}")
    endif()

    string(SUBSTRING "${smoke_stdout}" ${expected_prefix_length} -1 version_output)
    if(NOT version_output MATCHES "^clang version: clang version [^\n]+\nlld-link version: LLD [^\n]+\n$")
        message(FATAL_ERROR
            "${smoke_name} did not print clang and LLD-identifying version lines after the stable prefix\nactual suffix:\n${version_output}")
    endif()
endfunction()

function(assert_file_sha256_matches source_path installed_path description)
    if(NOT EXISTS "${source_path}")
        message(FATAL_ERROR "expected source ${description} does not exist: ${source_path}")
    endif()

    if(NOT EXISTS "${installed_path}")
        message(FATAL_ERROR "expected installed ${description} does not exist: ${installed_path}")
    endif()

    file(SHA256 "${source_path}" source_sha256)
    file(SHA256 "${installed_path}" installed_sha256)
    if(NOT installed_sha256 STREQUAL source_sha256)
        message(FATAL_ERROR
            "installed ${description} does not match source content\nsource: ${source_path}\ninstalled: ${installed_path}\nsource SHA256: ${source_sha256}\ninstalled SHA256: ${installed_sha256}")
    endif()
endfunction()

function(assert_expected_install_file_path_is_safe expected_file)
    if(expected_file STREQUAL "")
        message(FATAL_ERROR "expected install file entry is empty")
    endif()

    string(FIND "${expected_file}" "\\" backslash_index)
    if(NOT backslash_index EQUAL -1)
        message(FATAL_ERROR "expected install file uses a backslash path separator: ${expected_file}")
    endif()

    if(expected_file MATCHES "^/")
        message(FATAL_ERROR "expected install file uses an absolute path: ${expected_file}")
    endif()

    if(expected_file MATCHES "^[A-Za-z]:")
        message(FATAL_ERROR "expected install file uses a drive-letter absolute path: ${expected_file}")
    endif()

    if(expected_file MATCHES "//")
        message(FATAL_ERROR "expected install file contains an empty path component: ${expected_file}")
    endif()

    if(expected_file MATCHES "(^|/)\\.(/|$)")
        message(FATAL_ERROR "expected install file contains a '.' path component: ${expected_file}")
    endif()

    if(expected_file MATCHES "(^|/)\\.\\.(/|$)")
        message(FATAL_ERROR "expected install file contains a '..' path component: ${expected_file}")
    endif()

    if("${expected_file}" MATCHES "/$")
        message(FATAL_ERROR "expected install file must name a file, not a directory: ${expected_file}")
    endif()
endfunction()

function(assert_install_layout_contains_only_expected_entries)
    set(expected_installed_files
        "${INSTALL_BINDIR}/${EXECUTABLE_NAME}"
    )

    foreach(expected_doc IN LISTS EXPECTED_DOCS)
        list(APPEND expected_installed_files "${INSTALL_DOCDIR}/${expected_doc}")
    endforeach()

    set(seen_expected_installed_files)
    foreach(expected_file IN LISTS expected_installed_files)
        assert_expected_install_file_path_is_safe("${expected_file}")

        if(expected_file IN_LIST seen_expected_installed_files)
            message(FATAL_ERROR "expected install files contain a duplicate entry: ${expected_file}")
        endif()
        list(APPEND seen_expected_installed_files "${expected_file}")
    endforeach()

    set(expected_installed_entries ${expected_installed_files})
    foreach(expected_file IN LISTS expected_installed_files)
        set(current_path "${expected_file}")
        while(TRUE)
            string(REGEX REPLACE "/[^/]+$" "" parent_path "${current_path}")
            if(parent_path STREQUAL current_path)
                break()
            endif()

            list(APPEND expected_installed_entries "${parent_path}/")
            set(current_path "${parent_path}")
        endwhile()
    endforeach()

    list(REMOVE_DUPLICATES expected_installed_entries)

    file(GLOB_RECURSE installed_paths
        LIST_DIRECTORIES TRUE
        RELATIVE "${INSTALL_PREFIX}"
        "${INSTALL_PREFIX}/*"
    )

    set(actual_installed_entries)
    foreach(installed_path IN LISTS installed_paths)
        string(REPLACE "\\" "/" installed_entry "${installed_path}")
        if(IS_DIRECTORY "${INSTALL_PREFIX}/${installed_entry}")
            list(APPEND actual_installed_entries "${installed_entry}/")
        else()
            list(APPEND actual_installed_entries "${installed_entry}")
        endif()
    endforeach()

    foreach(installed_entry IN LISTS actual_installed_entries)
        if(NOT installed_entry IN_LIST expected_installed_entries)
            message(FATAL_ERROR "install layout contains unexpected entry: ${installed_entry}")
        endif()
    endforeach()

    foreach(expected_entry IN LISTS expected_installed_entries)
        if(NOT expected_entry IN_LIST actual_installed_entries)
            message(FATAL_ERROR "expected install layout entry missing: ${expected_entry}")
        endif()
    endforeach()
endfunction()

execute_process(
    COMMAND "${CMAKE_COMMAND}" "--install" "${BUILD_DIR}" "--prefix" "${INSTALL_PREFIX}"
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_stdout
    ERROR_VARIABLE install_stderr
)

if(NOT install_result EQUAL 0)
    message(FATAL_ERROR
        "install command failed with exit code ${install_result}\nstdout:\n${install_stdout}\nstderr:\n${install_stderr}")
endif()

assert_install_layout_contains_only_expected_entries()

set(installed_executable "${INSTALL_PREFIX}/${INSTALL_BINDIR}/${EXECUTABLE_NAME}")
if(NOT EXISTS "${installed_executable}")
    message(FATAL_ERROR "expected installed compiler does not exist: ${installed_executable}")
endif()

file(SIZE "${installed_executable}" installed_executable_size)
if(installed_executable_size LESS 1)
    message(FATAL_ERROR "expected installed compiler to be non-empty: ${installed_executable}")
endif()

foreach(expected_doc IN LISTS EXPECTED_DOCS)
    set(source_doc "${SOURCE_DIR}/${expected_doc}")
    set(installed_doc "${INSTALL_PREFIX}/${INSTALL_DOCDIR}/${expected_doc}")
    assert_file_sha256_matches("${source_doc}" "${installed_doc}" "doc ${expected_doc}")

    file(SIZE "${installed_doc}" installed_doc_size)
    if(installed_doc_size LESS 1)
        message(FATAL_ERROR "expected installed doc to be non-empty: ${installed_doc}")
    endif()
endforeach()

assert_compiler_stdout_matches(
    "installed compiler version smoke test"
    "${installed_executable}"
    "${EXPECTED_VERSION_PATH}"
    "--version")
assert_compiler_stdout_matches(
    "installed compiler help smoke test"
    "${installed_executable}"
    "${EXPECTED_HELP_PATH}"
    "--help")
assert_compiler_stdout_matches(
    "installed compiler toolchain smoke test"
    "${installed_executable}"
    "${EXPECTED_TOOLCHAIN_PATH}"
    "--print-toolchain")
assert_compiler_toolchain_check(
    "installed compiler toolchain probe smoke test"
    "${installed_executable}"
    "${EXPECTED_TOOLCHAIN_PATH}")
assert_compiler_stdout("installed compiler token smoke test" "${installed_executable}" "--dump-tokens" "${SMOKE_INPUT_PATH}")
