if(NOT DEFINED COMMAND_PATH)
    message(FATAL_ERROR "COMMAND_PATH is required")
endif()

if(NOT DEFINED INPUT_PATH AND NOT DEFINED INPUT_PATHS AND NOT DEFINED PACKAGE_DIR)
    message(FATAL_ERROR "INPUT_PATH, INPUT_PATHS, or PACKAGE_DIR is required")
endif()

if(NOT DEFINED EXPECTED_PATH)
    message(FATAL_ERROR "EXPECTED_PATH is required")
endif()

if(NOT DEFINED MODE)
    message(FATAL_ERROR "MODE is required")
endif()

if(DEFINED PACKAGE_DIR)
    set(input_args "--package" "${PACKAGE_DIR}")
    set(input_label "${PACKAGE_DIR}")
elseif(DEFINED INPUT_PATHS)
    set(input_args ${INPUT_PATHS})
    set(input_label "${INPUT_PATHS}")
else()
    set(input_args "${INPUT_PATH}")
    set(input_label "${INPUT_PATH}")
endif()

execute_process(
    COMMAND "${COMMAND_PATH}" "${MODE}" ${input_args}
    RESULT_VARIABLE command_result
    OUTPUT_VARIABLE command_stdout
    ERROR_VARIABLE command_stderr
)

if(NOT command_result EQUAL 0)
    message(FATAL_ERROR
        "compiler command failed with exit code ${command_result}\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
endif()

if(DEFINED NORMALIZE_BOOTSTRAP_SOURCE_COMMIT)
    if(NOT DEFINED SOURCE_DIR)
        get_filename_component(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    endif()

    execute_process(
        COMMAND git rev-parse HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE git_result
        OUTPUT_VARIABLE source_commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT git_result EQUAL 0 OR source_commit STREQUAL "")
        message(FATAL_ERROR "failed to determine bootstrap source commit for normalization")
    endif()
    string(LENGTH "${source_commit}" source_commit_length)
    if(NOT source_commit_length EQUAL 40)
        message(FATAL_ERROR "unexpected bootstrap source commit '${source_commit}'")
    endif()

    string(REGEX REPLACE "\"${source_commit}\"" "\"source commit recorded in release evidence\"" command_stdout "${command_stdout}")
endif()

file(READ "${EXPECTED_PATH}" expected_output)
if(NOT command_stdout STREQUAL expected_output)
    message(FATAL_ERROR
        "output mismatch for ${input_label}\nexpected:\n${expected_output}\nactual:\n${command_stdout}")
endif()
