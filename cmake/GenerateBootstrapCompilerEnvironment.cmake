if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

if(NOT DEFINED OUTPUT_PATH)
    message(FATAL_ERROR "OUTPUT_PATH is required")
endif()

set(source_environment_path "${SOURCE_DIR}/bootstrap/compiler/src/environment.evd")

if(NOT EXISTS "${source_environment_path}")
    message(FATAL_ERROR "source environment file not found: ${source_environment_path}")
endif()

execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE git_result
    OUTPUT_VARIABLE source_commit
    ERROR_VARIABLE git_stderr
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT git_result EQUAL 0 OR source_commit STREQUAL "")
    message(WARNING "could not determine source commit from git; using the source-tree placeholder")
    set(source_commit "source commit recorded in release evidence")
endif()

file(READ "${source_environment_path}" environment_text)
string(REPLACE "source commit recorded in release evidence" "${source_commit}" environment_text "${environment_text}")

get_filename_component(output_dir "${OUTPUT_PATH}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
file(WRITE "${OUTPUT_PATH}" "${environment_text}")
