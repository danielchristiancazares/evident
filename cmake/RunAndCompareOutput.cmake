if(NOT DEFINED COMMAND_PATH)
    message(FATAL_ERROR "COMMAND_PATH is required")
endif()

if(NOT DEFINED INPUT_PATH)
    message(FATAL_ERROR "INPUT_PATH is required")
endif()

if(NOT DEFINED EXPECTED_PATH)
    message(FATAL_ERROR "EXPECTED_PATH is required")
endif()

if(NOT DEFINED MODE)
    message(FATAL_ERROR "MODE is required")
endif()

execute_process(
    COMMAND "${COMMAND_PATH}" "${MODE}" "${INPUT_PATH}"
    RESULT_VARIABLE command_result
    OUTPUT_VARIABLE command_stdout
    ERROR_VARIABLE command_stderr
)

if(NOT command_result EQUAL 0)
    message(FATAL_ERROR
        "compiler command failed with exit code ${command_result}\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
endif()

file(READ "${EXPECTED_PATH}" expected_output)
if(NOT command_stdout STREQUAL expected_output)
    message(FATAL_ERROR
        "output mismatch for ${INPUT_PATH}\nexpected:\n${expected_output}\nactual:\n${command_stdout}")
endif()
