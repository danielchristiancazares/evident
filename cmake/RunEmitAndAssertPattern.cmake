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

if(NOT command_stdout STREQUAL "")
    message(FATAL_ERROR
        "compiler command wrote unexpected stdout for ${INPUT_PATH}\nstdout:\n${command_stdout}")
endif()

if(NOT command_stderr STREQUAL "")
    message(FATAL_ERROR
        "compiler command wrote unexpected stderr for ${INPUT_PATH}\nstderr:\n${command_stderr}")
endif()

if(NOT EXISTS "${OUTPUT_PATH}")
    message(FATAL_ERROR "expected emitted file does not exist: ${OUTPUT_PATH}")
endif()

file(SIZE "${OUTPUT_PATH}" output_size)
if(output_size LESS 1)
    message(FATAL_ERROR "expected emitted file to be non-empty: ${OUTPUT_PATH}")
endif()

file(READ "${OUTPUT_PATH}" actual_output)

if(DEFINED CONTAINS AND NOT CONTAINS STREQUAL "")
    string(FIND "${actual_output}" "${CONTAINS}" contains_index)
    if(contains_index EQUAL -1)
        message(FATAL_ERROR
            "expected output for ${INPUT_PATH} to contain:\n${CONTAINS}\nactual:\n${actual_output}")
    endif()
endif()

foreach(contains_pattern IN LISTS CONTAINS_PATTERNS)
    string(FIND "${actual_output}" "${contains_pattern}" contains_index)
    if(contains_index EQUAL -1)
        message(FATAL_ERROR
            "expected output for ${INPUT_PATH} to contain:\n${contains_pattern}\nactual:\n${actual_output}")
    endif()
endforeach()

if(DEFINED NOT_CONTAINS AND NOT NOT_CONTAINS STREQUAL "")
    string(FIND "${actual_output}" "${NOT_CONTAINS}" not_contains_index)
    if(NOT not_contains_index EQUAL -1)
        message(FATAL_ERROR
            "expected output for ${INPUT_PATH} not to contain:\n${NOT_CONTAINS}\nactual:\n${actual_output}")
    endif()
endif()

foreach(not_contains_pattern IN LISTS NOT_CONTAINS_PATTERNS)
    string(FIND "${actual_output}" "${not_contains_pattern}" not_contains_index)
    if(NOT not_contains_index EQUAL -1)
        message(FATAL_ERROR
            "expected output for ${INPUT_PATH} not to contain:\n${not_contains_pattern}\nactual:\n${actual_output}")
    endif()
endforeach()
