if(NOT DEFINED COMMAND_PATH)
    message(FATAL_ERROR "COMMAND_PATH is required")
endif()

if(NOT DEFINED INPUT_PATH)
    message(FATAL_ERROR "INPUT_PATH is required")
endif()

if(NOT DEFINED OUTPUT_PATH)
    message(FATAL_ERROR "OUTPUT_PATH is required")
endif()

if(NOT DEFINED EXPECTED_PATH)
    message(FATAL_ERROR "EXPECTED_PATH is required")
endif()

if(NOT DEFINED EMIT_MODE)
    message(FATAL_ERROR "EMIT_MODE is required")
endif()

if(NOT DEFINED TARGET_TRIPLE)
    set(TARGET_TRIPLE x86_64-pc-windows-msvc)
endif()

execute_process(
    COMMAND "${COMMAND_PATH}" "--target" "${TARGET_TRIPLE}" "${EMIT_MODE}" "${OUTPUT_PATH}" "${INPUT_PATH}"
    RESULT_VARIABLE command_result
    OUTPUT_VARIABLE command_stdout
    ERROR_VARIABLE command_stderr
)

if(NOT command_result EQUAL 0)
    message(FATAL_ERROR
        "compiler command failed with exit code ${command_result}\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
endif()

file(READ "${EXPECTED_PATH}" expected_output)
file(READ "${OUTPUT_PATH}" actual_output)
string(REGEX REPLACE "[\r\n]+$" "" expected_output "${expected_output}")
string(REGEX REPLACE "[\r\n]+$" "" actual_output "${actual_output}")
if(NOT actual_output STREQUAL expected_output)
    message(FATAL_ERROR
        "output mismatch for ${INPUT_PATH}\nexpected:\n${expected_output}\nactual:\n${actual_output}")
endif()
