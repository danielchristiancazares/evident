if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

if(NOT DEFINED PACKAGE_PATH)
    message(FATAL_ERROR "PACKAGE_PATH is required")
endif()

if(NOT DEFINED CHECKSUM_PATH)
    message(FATAL_ERROR "CHECKSUM_PATH is required")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" "--build" "${BUILD_DIR}" "--target" "package_checksum"
    RESULT_VARIABLE checksum_result
    OUTPUT_VARIABLE checksum_stdout
    ERROR_VARIABLE checksum_stderr
)

if(NOT checksum_result EQUAL 0)
    message(FATAL_ERROR
        "package checksum target failed with exit code ${checksum_result}\nstdout:\n${checksum_stdout}\nstderr:\n${checksum_stderr}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/AssertPackageChecksum.cmake")
