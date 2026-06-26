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

if(command_result EQUAL 0)
    message(FATAL_ERROR
        "compiler command unexpectedly succeeded\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
endif()

set(actual_output "${command_stderr}")
string(REPLACE "\r\n" "\n" actual_output "${actual_output}")
string(REPLACE "\r" "\n" actual_output "${actual_output}")
string(REPLACE "\\" "/" actual_output "${actual_output}")
string(REGEX REPLACE "[\n]+$" "\n" actual_output "${actual_output}")

if(DEFINED NORMALIZE_PATHS)
    set(path_index 1)
    foreach(path IN LISTS NORMALIZE_PATHS)
        file(TO_CMAKE_PATH "${path}" path_cmake)
        file(TO_NATIVE_PATH "${path}" path_native)
        string(REPLACE "${path}" "<path${path_index}>" actual_output "${actual_output}")
        string(REPLACE "${path_cmake}" "<path${path_index}>" actual_output "${actual_output}")
        string(REPLACE "${path_native}" "<path${path_index}>" actual_output "${actual_output}")
        math(EXPR path_index "${path_index} + 1")
    endforeach()
endif()

file(READ "${EXPECTED_PATH}" expected_output)
string(REPLACE "\r\n" "\n" expected_output "${expected_output}")
string(REPLACE "\r" "\n" expected_output "${expected_output}")
string(REGEX REPLACE "[\n]+$" "\n" expected_output "${expected_output}")

if(NOT actual_output STREQUAL expected_output)
    message(FATAL_ERROR
        "CLI diagnostic mismatch\nexpected:\n${expected_output}\nactual:\n${actual_output}")
endif()
