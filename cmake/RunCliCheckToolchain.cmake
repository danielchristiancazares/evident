if(NOT DEFINED COMMAND_PATH)
    message(FATAL_ERROR "COMMAND_PATH is required")
endif()

if(NOT DEFINED EXPECTED_PREFIX_PATH)
    message(FATAL_ERROR "EXPECTED_PREFIX_PATH is required")
endif()

execute_process(
    COMMAND "${COMMAND_PATH}" --check-toolchain
    RESULT_VARIABLE command_result
    OUTPUT_VARIABLE command_stdout
    ERROR_VARIABLE command_stderr
)

if(NOT command_result EQUAL 0)
    message(FATAL_ERROR
        "toolchain check failed with exit code ${command_result}\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
endif()

if(NOT command_stderr STREQUAL "")
    message(FATAL_ERROR
        "toolchain check wrote unexpected stderr\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
endif()

set(actual_output "${command_stdout}")
string(REPLACE "\r\n" "\n" actual_output "${actual_output}")
string(REPLACE "\r" "\n" actual_output "${actual_output}")
string(REGEX REPLACE "[\n]+$" "\n" actual_output "${actual_output}")

file(READ "${EXPECTED_PREFIX_PATH}" expected_prefix)
string(REPLACE "\r\n" "\n" expected_prefix "${expected_prefix}")
string(REPLACE "\r" "\n" expected_prefix "${expected_prefix}")
string(REGEX REPLACE "[\n]+$" "\n" expected_prefix "${expected_prefix}")

string(LENGTH "${expected_prefix}" expected_prefix_length)
string(SUBSTRING "${actual_output}" 0 ${expected_prefix_length} actual_prefix)
if(NOT actual_prefix STREQUAL expected_prefix)
    message(FATAL_ERROR
        "toolchain check stable output prefix mismatch\nexpected prefix:\n${expected_prefix}\nactual:\n${actual_output}")
endif()

string(SUBSTRING "${actual_output}" ${expected_prefix_length} -1 version_output)
if(NOT version_output MATCHES "^clang version: clang version [^\n]+\nlld-link version: LLD [^\n]+\n$")
    message(FATAL_ERROR
        "toolchain check did not print clang and LLD-identifying version lines after the stable prefix\nactual suffix:\n${version_output}")
endif()
