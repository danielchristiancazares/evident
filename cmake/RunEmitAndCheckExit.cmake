if(NOT DEFINED COMMAND_PATH)
    message(FATAL_ERROR "COMMAND_PATH is required")
endif()

if(NOT DEFINED INPUT_PATH AND NOT DEFINED INPUT_PATHS AND NOT DEFINED PACKAGE_DIR)
    message(FATAL_ERROR "INPUT_PATH, INPUT_PATHS, or PACKAGE_DIR is required")
endif()

if(NOT DEFINED OUTPUT_PATH)
    message(FATAL_ERROR "OUTPUT_PATH is required")
endif()

if(NOT DEFINED EXPECTED_EXIT)
    message(FATAL_ERROR "EXPECTED_EXIT is required")
endif()

if(NOT DEFINED TARGET_TRIPLE)
    set(TARGET_TRIPLE x86_64-pc-windows-msvc)
endif()

if(NOT DEFINED EXPECTED_STDOUT)
    set(EXPECTED_STDOUT "")
endif()

if(NOT DEFINED EXPECTED_STDERR)
    set(EXPECTED_STDERR "")
endif()

function(normalize_text text out_var)
    set(normalized "${text}")
    string(REPLACE "\r\n" "\n" normalized "${normalized}")
    string(REPLACE "\r" "\n" normalized "${normalized}")
    set("${out_var}" "${normalized}" PARENT_SCOPE)
endfunction()

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
    COMMAND "${COMMAND_PATH}" "--target" "${TARGET_TRIPLE}" "--emit-exe" "${OUTPUT_PATH}" ${input_args}
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)

if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR
        "compiler command failed with exit code ${compile_result}\nstdout:\n${compile_stdout}\nstderr:\n${compile_stderr}")
endif()

if(NOT compile_stdout STREQUAL "")
    message(FATAL_ERROR
        "compiler command wrote unexpected stdout for ${input_label}\nstdout:\n${compile_stdout}")
endif()

if(NOT compile_stderr STREQUAL "")
    message(FATAL_ERROR
        "compiler command wrote unexpected stderr for ${input_label}\nstderr:\n${compile_stderr}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/AssertPeExecutableFile.cmake")
assert_pe_executable_file("${OUTPUT_PATH}")

execute_process(
    COMMAND "${OUTPUT_PATH}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
)

if(NOT run_result EQUAL EXPECTED_EXIT)
    message(FATAL_ERROR
        "executable exit code mismatch for ${input_label}\nexpected: ${EXPECTED_EXIT}\nactual: ${run_result}\nstdout:\n${run_stdout}\nstderr:\n${run_stderr}")
endif()

normalize_text("${EXPECTED_STDOUT}" expected_stdout)
normalize_text("${run_stdout}" actual_stdout)
if(NOT actual_stdout STREQUAL expected_stdout)
    message(FATAL_ERROR
        "executable stdout mismatch for ${input_label}\nexpected:\n${expected_stdout}\nactual:\n${actual_stdout}")
endif()

normalize_text("${EXPECTED_STDERR}" expected_stderr)
normalize_text("${run_stderr}" actual_stderr)
if(NOT actual_stderr STREQUAL expected_stderr)
    message(FATAL_ERROR
        "executable stderr mismatch for ${input_label}\nexpected:\n${expected_stderr}\nactual:\n${actual_stderr}")
endif()
