if(NOT DEFINED COMMAND_PATH)
    message(FATAL_ERROR "COMMAND_PATH is required")
endif()

if(NOT DEFINED INPUT_PATH AND NOT DEFINED INPUT_PATHS)
    message(FATAL_ERROR "INPUT_PATH or INPUT_PATHS is required")
endif()

if(NOT DEFINED EXPECTED_PATH)
    message(FATAL_ERROR "EXPECTED_PATH is required")
endif()

if(DEFINED INPUT_PATHS)
    set(input_args ${INPUT_PATHS})
    set(input_label "${INPUT_PATHS}")
else()
    set(input_args "${INPUT_PATH}")
    set(input_label "${INPUT_PATH}")
endif()

if(DEFINED EMIT_MODE)
    if(NOT DEFINED OUTPUT_PATH)
        message(FATAL_ERROR "OUTPUT_PATH is required when EMIT_MODE is set")
    endif()
    if(NOT DEFINED TARGET_TRIPLE)
        set(TARGET_TRIPLE x86_64-pc-windows-msvc)
    endif()

    set(command_prefix)
    if(DEFINED TOOLCHAIN_CLANG)
        list(APPEND command_prefix "${CMAKE_COMMAND}" "-E" "env" "EVIDENT_CLANG=${TOOLCHAIN_CLANG}")
    endif()
    execute_process(
        COMMAND ${command_prefix} "${COMMAND_PATH}" "--target" "${TARGET_TRIPLE}" "${EMIT_MODE}" "${OUTPUT_PATH}" ${input_args}
        RESULT_VARIABLE command_result
        OUTPUT_VARIABLE command_stdout
        ERROR_VARIABLE command_stderr
    )
else()
    execute_process(
        COMMAND "${COMMAND_PATH}" ${input_args}
        RESULT_VARIABLE command_result
        OUTPUT_VARIABLE command_stdout
        ERROR_VARIABLE command_stderr
    )
endif()

if(command_result EQUAL 0)
    message(FATAL_ERROR
        "compiler command unexpectedly succeeded for ${input_label}\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
endif()

set(actual_output "${command_stderr}")
string(REPLACE "\r\n" "\n" actual_output "${actual_output}")
string(REPLACE "\r" "\n" actual_output "${actual_output}")
string(REGEX REPLACE "[\n]+$" "\n" actual_output "${actual_output}")

if(DEFINED INPUT_PATHS)
    set(input_index 1)
    foreach(input_path IN LISTS INPUT_PATHS)
        file(TO_CMAKE_PATH "${input_path}" input_path_cmake)
        file(TO_NATIVE_PATH "${input_path}" input_path_native)
        string(REPLACE "${input_path}" "<input${input_index}>" actual_output "${actual_output}")
        string(REPLACE "${input_path_cmake}" "<input${input_index}>" actual_output "${actual_output}")
        string(REPLACE "${input_path_native}" "<input${input_index}>" actual_output "${actual_output}")
        math(EXPR input_index "${input_index} + 1")
    endforeach()
else()
    file(TO_CMAKE_PATH "${INPUT_PATH}" input_path_cmake)
    file(TO_NATIVE_PATH "${INPUT_PATH}" input_path_native)
    string(REPLACE "${INPUT_PATH}" "<input>" actual_output "${actual_output}")
    string(REPLACE "${input_path_cmake}" "<input>" actual_output "${actual_output}")
    string(REPLACE "${input_path_native}" "<input>" actual_output "${actual_output}")
endif()
if(DEFINED OUTPUT_PATH)
    file(TO_CMAKE_PATH "${OUTPUT_PATH}" output_path_cmake)
    file(TO_NATIVE_PATH "${OUTPUT_PATH}" output_path_native)
    string(REPLACE "${OUTPUT_PATH}" "<output>" actual_output "${actual_output}")
    string(REPLACE "${output_path_cmake}" "<output>" actual_output "${actual_output}")
    string(REPLACE "${output_path_native}" "<output>" actual_output "${actual_output}")
endif()

file(READ "${EXPECTED_PATH}" expected_output)
string(REPLACE "\r\n" "\n" expected_output "${expected_output}")
string(REPLACE "\r" "\n" expected_output "${expected_output}")
string(REGEX REPLACE "[\n]+$" "\n" expected_output "${expected_output}")

if(NOT actual_output STREQUAL expected_output)
    message(FATAL_ERROR
        "diagnostic mismatch for ${input_label}\nexpected:\n${expected_output}\nactual:\n${actual_output}")
endif()
