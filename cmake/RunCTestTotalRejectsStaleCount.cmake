if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

if(NOT DEFINED CTEST_COMMAND)
    message(FATAL_ERROR "CTEST_COMMAND is required")
endif()

if(NOT DEFINED EXPECTED_CTEST_TOTAL)
    message(FATAL_ERROR "EXPECTED_CTEST_TOTAL is required")
endif()

if(NOT EXPECTED_CTEST_TOTAL MATCHES "^[2-9][0-9]*$")
    message(FATAL_ERROR "EXPECTED_CTEST_TOTAL must be at least 2 for stale-count validation: ${EXPECTED_CTEST_TOTAL}")
endif()

math(EXPR stale_ctest_total "${EXPECTED_CTEST_TOTAL} - 1")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DBUILD_DIR=${BUILD_DIR}"
        "-DCTEST_COMMAND=${CTEST_COMMAND}"
        "-DEXPECTED_CTEST_TOTAL=${stale_ctest_total}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertCTestTotal.cmake"
    RESULT_VARIABLE validation_result
    OUTPUT_VARIABLE validation_stdout
    ERROR_VARIABLE validation_stderr
)

if(validation_result EQUAL 0)
    message(FATAL_ERROR
        "CTest total validation unexpectedly accepted stale count ${stale_ctest_total}\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
endif()

set(actual_error "${validation_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")

foreach(required_fragment IN ITEMS
    "expected CTest total ${stale_ctest_total}"
    "ctest -N reported"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "stale CTest total validation error did not contain required fragment '${required_fragment}'\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
    endif()
endforeach()
