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

if(NOT EXISTS "${OUTPUT_PATH}")
    message(FATAL_ERROR "expected emitted file does not exist: ${OUTPUT_PATH}")
endif()

file(SIZE "${OUTPUT_PATH}" output_size)
if(output_size LESS 1)
    message(FATAL_ERROR "expected emitted file to be non-empty: ${OUTPUT_PATH}")
endif()
