if(NOT DEFINED COMMAND_PATH)
    message(FATAL_ERROR "COMMAND_PATH is required")
endif()

if(NOT DEFINED INPUT_PATH)
    message(FATAL_ERROR "INPUT_PATH is required")
endif()

if(NOT DEFINED OUTPUT_PATH)
    message(FATAL_ERROR "OUTPUT_PATH is required")
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

file(READ "${OUTPUT_PATH}" actual_output)

if(DEFINED CONTAINS AND NOT CONTAINS STREQUAL "")
    string(FIND "${actual_output}" "${CONTAINS}" contains_index)
    if(contains_index EQUAL -1)
        message(FATAL_ERROR
            "expected output for ${INPUT_PATH} to contain:\n${CONTAINS}\nactual:\n${actual_output}")
    endif()
endif()

if(DEFINED NOT_CONTAINS AND NOT NOT_CONTAINS STREQUAL "")
    string(FIND "${actual_output}" "${NOT_CONTAINS}" not_contains_index)
    if(NOT not_contains_index EQUAL -1)
        message(FATAL_ERROR
            "expected output for ${INPUT_PATH} not to contain:\n${NOT_CONTAINS}\nactual:\n${actual_output}")
    endif()
endif()
