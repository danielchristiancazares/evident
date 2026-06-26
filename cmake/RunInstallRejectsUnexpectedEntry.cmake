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

string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" install_suffix)
set(install_prefix "${BUILD_DIR}/install-layout-unexpected-${install_suffix}")
set(unexpected_entry "${install_prefix}/unexpected-release-payload.txt")
file(MAKE_DIRECTORY "${install_prefix}")
file(WRITE "${unexpected_entry}" "unexpected release payload\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DBUILD_DIR=${BUILD_DIR}"
        "-DEXECUTABLE_NAME=${EXECUTABLE_NAME}"
        "-DSOURCE_DIR=${SOURCE_DIR}"
        "-DINSTALL_BINDIR=${INSTALL_BINDIR}"
        "-DINSTALL_DOCDIR=${INSTALL_DOCDIR}"
        "-DINSTALL_PREFIX=${install_prefix}"
        "-DSMOKE_INPUT_PATH=${SMOKE_INPUT_PATH}"
        "-DEXPECTED_VERSION_PATH=${EXPECTED_VERSION_PATH}"
        "-DEXPECTED_HELP_PATH=${EXPECTED_HELP_PATH}"
        "-DEXPECTED_TOOLCHAIN_PATH=${EXPECTED_TOOLCHAIN_PATH}"
        "-DEXPECTED_DOCS=${EXPECTED_DOCS}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/RunInstallAndAssertLayout.cmake"
    RESULT_VARIABLE validation_result
    OUTPUT_VARIABLE validation_stdout
    ERROR_VARIABLE validation_stderr
)

if(validation_result EQUAL 0)
    message(FATAL_ERROR
        "install-layout validation unexpectedly accepted an unexpected installed entry\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
endif()

set(actual_error "${validation_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")

foreach(required_fragment IN ITEMS
    "install layout contains unexpected entry"
    "unexpected-release-payload.txt")
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "unexpected install-layout error did not contain required fragment '${required_fragment}'\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
    endif()
endforeach()

function(assert_expected_install_docs_rejected case_name expected_docs)
    set(case_install_prefix "${BUILD_DIR}/install-layout-${case_name}-${install_suffix}")

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DBUILD_DIR=${BUILD_DIR}"
            "-DEXECUTABLE_NAME=${EXECUTABLE_NAME}"
            "-DSOURCE_DIR=${SOURCE_DIR}"
            "-DINSTALL_BINDIR=${INSTALL_BINDIR}"
            "-DINSTALL_DOCDIR=${INSTALL_DOCDIR}"
            "-DINSTALL_PREFIX=${case_install_prefix}"
            "-DSMOKE_INPUT_PATH=${SMOKE_INPUT_PATH}"
            "-DEXPECTED_VERSION_PATH=${EXPECTED_VERSION_PATH}"
            "-DEXPECTED_HELP_PATH=${EXPECTED_HELP_PATH}"
            "-DEXPECTED_TOOLCHAIN_PATH=${EXPECTED_TOOLCHAIN_PATH}"
            "-DEXPECTED_DOCS=${expected_docs}"
            "-P" "${CMAKE_CURRENT_LIST_DIR}/RunInstallAndAssertLayout.cmake"
        RESULT_VARIABLE expected_docs_result
        OUTPUT_VARIABLE expected_docs_stdout
        ERROR_VARIABLE expected_docs_stderr
    )

    if(expected_docs_result EQUAL 0)
        message(FATAL_ERROR
            "${case_name} install-layout validation unexpectedly accepted unsafe expected install docs\nstdout:\n${expected_docs_stdout}\nstderr:\n${expected_docs_stderr}")
    endif()

    set(actual_error "${expected_docs_stderr}")
    string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
    string(REPLACE "\r" "\n" actual_error "${actual_error}")

    foreach(required_fragment IN LISTS ARGN)
        string(FIND "${actual_error}" "${required_fragment}" fragment_index)
        if(fragment_index EQUAL -1)
            message(FATAL_ERROR
                "${case_name} expected install docs error did not contain required fragment '${required_fragment}'\nstdout:\n${expected_docs_stdout}\nstderr:\n${expected_docs_stderr}")
        endif()
    endforeach()
endfunction()

assert_expected_install_docs_rejected(
    "parent-traversal-expected-doc"
    "README.md;../LICENSE"
    "expected install file contains a '..' path component:"
    "${INSTALL_DOCDIR}/../LICENSE"
)

assert_expected_install_docs_rejected(
    "empty-component-expected-doc"
    "README.md;docs//BROKEN.md"
    "expected install file contains an empty path component:"
    "${INSTALL_DOCDIR}/docs//BROKEN.md"
)

assert_expected_install_docs_rejected(
    "duplicate-expected-doc"
    "README.md;README.md"
    "expected install files contain a duplicate entry:"
    "${INSTALL_DOCDIR}/README.md"
)
