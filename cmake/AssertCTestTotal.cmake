if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

if(NOT DEFINED CTEST_COMMAND)
    message(FATAL_ERROR "CTEST_COMMAND is required")
endif()

if(NOT DEFINED EXPECTED_CTEST_TOTAL)
    message(FATAL_ERROR "EXPECTED_CTEST_TOTAL is required")
endif()

if(NOT EXISTS "${BUILD_DIR}")
    message(FATAL_ERROR "expected build directory does not exist: ${BUILD_DIR}")
endif()

if(NOT EXISTS "${CTEST_COMMAND}")
    message(FATAL_ERROR "expected CTest command does not exist: ${CTEST_COMMAND}")
endif()

if(NOT EXPECTED_CTEST_TOTAL MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "EXPECTED_CTEST_TOTAL must be a positive decimal test count: ${EXPECTED_CTEST_TOTAL}")
endif()

execute_process(
    COMMAND "${CTEST_COMMAND}" -N
    WORKING_DIRECTORY "${BUILD_DIR}"
    RESULT_VARIABLE ctest_result
    OUTPUT_VARIABLE ctest_stdout
    ERROR_VARIABLE ctest_stderr
)

if(NOT ctest_result EQUAL 0)
    message(FATAL_ERROR
        "ctest -N failed with exit code ${ctest_result}\nstdout:\n${ctest_stdout}\nstderr:\n${ctest_stderr}")
endif()

set(ctest_output "${ctest_stdout}\n${ctest_stderr}")
string(REPLACE "\r\n" "\n" ctest_output "${ctest_output}")
string(REPLACE "\r" "\n" ctest_output "${ctest_output}")

string(REGEX MATCH "Total Tests:[ ]*([0-9]+)" ctest_total_match "${ctest_output}")
if(ctest_total_match STREQUAL "")
    message(FATAL_ERROR "ctest -N output did not contain a Total Tests line\noutput:\n${ctest_output}")
endif()

set(actual_ctest_total "${CMAKE_MATCH_1}")
if(NOT actual_ctest_total STREQUAL "${EXPECTED_CTEST_TOTAL}")
    message(FATAL_ERROR
        "expected CTest total ${EXPECTED_CTEST_TOTAL} but ctest -N reported ${actual_ctest_total}")
endif()
