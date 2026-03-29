if(NOT DEFINED COMMAND_PATH)
    message(FATAL_ERROR "COMMAND_PATH is required")
endif()

if(NOT DEFINED INPUT_PATH)
    message(FATAL_ERROR "INPUT_PATH is required")
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

execute_process(
    COMMAND "${COMMAND_PATH}" "--target" "${TARGET_TRIPLE}" "--emit-exe" "${OUTPUT_PATH}" "${INPUT_PATH}"
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_stdout
    ERROR_VARIABLE compile_stderr
)

if(NOT compile_result EQUAL 0)
    message(FATAL_ERROR
        "compiler command failed with exit code ${compile_result}\nstdout:\n${compile_stdout}\nstderr:\n${compile_stderr}")
endif()

execute_process(
    COMMAND "${OUTPUT_PATH}"
    RESULT_VARIABLE run_result
    OUTPUT_VARIABLE run_stdout
    ERROR_VARIABLE run_stderr
)

if(NOT run_result EQUAL EXPECTED_EXIT)
    message(FATAL_ERROR
        "executable exit code mismatch for ${INPUT_PATH}\nexpected: ${EXPECTED_EXIT}\nactual: ${run_result}\nstdout:\n${run_stdout}\nstderr:\n${run_stderr}")
endif()
