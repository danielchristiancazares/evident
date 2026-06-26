if(NOT DEFINED COMMAND_PATH)
    message(FATAL_ERROR "COMMAND_PATH is required")
endif()

if(NOT DEFINED EXPECTED_PATH)
    message(FATAL_ERROR "EXPECTED_PATH is required")
endif()

if(DEFINED COMMAND_ARGS)
    set(command_args ${COMMAND_ARGS})
else()
    set(command_args)
endif()

execute_process(
    COMMAND "${COMMAND_PATH}" ${command_args}
    RESULT_VARIABLE command_result
    OUTPUT_VARIABLE command_stdout
    ERROR_VARIABLE command_stderr
)

if(NOT command_result EQUAL 0)
    message(FATAL_ERROR
        "compiler command failed with exit code ${command_result}\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
endif()

if(NOT command_stderr STREQUAL "")
    message(FATAL_ERROR
        "compiler command wrote unexpected stderr\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
endif()

set(actual_output "${command_stdout}")
string(REPLACE "\r\n" "\n" actual_output "${actual_output}")
string(REPLACE "\r" "\n" actual_output "${actual_output}")
string(REGEX REPLACE "[\n]+$" "\n" actual_output "${actual_output}")

file(READ "${EXPECTED_PATH}" expected_output)
string(REPLACE "\r\n" "\n" expected_output "${expected_output}")
string(REPLACE "\r" "\n" expected_output "${expected_output}")
string(REGEX REPLACE "[\n]+$" "\n" expected_output "${expected_output}")

if(NOT actual_output STREQUAL expected_output)
    message(FATAL_ERROR
        "CLI output mismatch\nexpected:\n${expected_output}\nactual:\n${actual_output}")
endif()
