if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

if(NOT DEFINED PACKAGE_PATH)
    message(FATAL_ERROR "PACKAGE_PATH is required")
endif()

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

if(NOT DEFINED EXPECTED_ENTRIES)
    message(FATAL_ERROR "EXPECTED_ENTRIES is required")
endif()

if(NOT DEFINED EXPECTED_DOCS)
    message(FATAL_ERROR "EXPECTED_DOCS is required")
endif()

if(NOT DEFINED EXECUTABLE_ENTRY)
    message(FATAL_ERROR "EXECUTABLE_ENTRY is required")
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

function(assert_file_sha256_matches source_path packaged_path description)
    if(NOT EXISTS "${source_path}")
        message(FATAL_ERROR "expected source ${description} does not exist: ${source_path}")
    endif()

    if(NOT EXISTS "${packaged_path}")
        message(FATAL_ERROR "expected packaged ${description} does not exist after extraction: ${packaged_path}")
    endif()

    file(SHA256 "${source_path}" source_sha256)
    file(SHA256 "${packaged_path}" packaged_sha256)
    if(NOT packaged_sha256 STREQUAL source_sha256)
        message(FATAL_ERROR
            "packaged ${description} does not match source content\nsource: ${source_path}\npackaged: ${packaged_path}\nsource SHA256: ${source_sha256}\npackaged SHA256: ${packaged_sha256}")
    endif()
endfunction()

execute_process(
    COMMAND "${CMAKE_COMMAND}" "--build" "${BUILD_DIR}" "--target" "package"
    RESULT_VARIABLE package_result
    OUTPUT_VARIABLE package_stdout
    ERROR_VARIABLE package_stderr
)

if(NOT package_result EQUAL 0)
    message(FATAL_ERROR
        "package target failed with exit code ${package_result}\nstdout:\n${package_stdout}\nstderr:\n${package_stderr}")
endif()

if(NOT EXISTS "${PACKAGE_PATH}")
    message(FATAL_ERROR "expected package ZIP does not exist: ${PACKAGE_PATH}")
endif()

file(SIZE "${PACKAGE_PATH}" package_size)
if(package_size LESS 1)
    message(FATAL_ERROR "expected package ZIP to be non-empty: ${PACKAGE_PATH}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/AssertPackageZipListing.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/AssertPeExecutableFile.cmake")

string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" extract_suffix)
set(EXTRACT_DIR "${BUILD_DIR}/package-layout-${extract_suffix}")
file(MAKE_DIRECTORY "${EXTRACT_DIR}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" "-E" "tar" "xf" "${PACKAGE_PATH}"
    WORKING_DIRECTORY "${EXTRACT_DIR}"
    RESULT_VARIABLE extract_result
    OUTPUT_VARIABLE extract_stdout
    ERROR_VARIABLE extract_stderr
)

if(NOT extract_result EQUAL 0)
    message(FATAL_ERROR
        "failed to extract package ZIP ${PACKAGE_PATH} with exit code ${extract_result}\nstdout:\n${extract_stdout}\nstderr:\n${extract_stderr}")
endif()

set(packaged_executable "${EXTRACT_DIR}/${EXECUTABLE_ENTRY}")
assert_pe_executable_file("${packaged_executable}")

foreach(expected_doc IN LISTS EXPECTED_DOCS)
    set(source_doc "${SOURCE_DIR}/${expected_doc}")
    set(packaged_doc "${EXTRACT_DIR}/${PACKAGE_ROOT}/${INSTALL_DOCDIR}/${expected_doc}")
    assert_file_sha256_matches("${source_doc}" "${packaged_doc}" "doc ${expected_doc}")

    file(SIZE "${packaged_doc}" packaged_doc_size)
    if(packaged_doc_size LESS 1)
        message(FATAL_ERROR "expected packaged doc to be non-empty: ${packaged_doc}")
    endif()
endforeach()

assert_compiler_stdout_matches(
    "packaged compiler version smoke test"
    "${packaged_executable}"
    "${EXPECTED_VERSION_PATH}"
    "--version")
assert_compiler_stdout_matches(
    "packaged compiler help smoke test"
    "${packaged_executable}"
    "${EXPECTED_HELP_PATH}"
    "--help")
assert_compiler_stdout_matches(
    "packaged compiler toolchain smoke test"
    "${packaged_executable}"
    "${EXPECTED_TOOLCHAIN_PATH}"
    "--print-toolchain")
assert_compiler_toolchain_check(
    "packaged compiler toolchain probe smoke test"
    "${packaged_executable}"
    "${EXPECTED_TOOLCHAIN_PATH}")
assert_compiler_stdout("packaged compiler token smoke test" "${packaged_executable}" "--dump-tokens" "${SMOKE_INPUT_PATH}")
