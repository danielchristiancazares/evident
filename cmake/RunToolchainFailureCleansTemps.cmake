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

if(NOT DEFINED TOOLCHAIN_CLANG)
    message(FATAL_ERROR "TOOLCHAIN_CLANG is required")
endif()

set(temp_ir_path "${OUTPUT_PATH}.tmp.ll")
set(log_path "${OUTPUT_PATH}.tool.log")

file(REMOVE "${OUTPUT_PATH}" "${temp_ir_path}" "${log_path}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "EVIDENT_CLANG=${TOOLCHAIN_CLANG}"
        "${COMMAND_PATH}" --target x86_64-pc-windows-msvc "${EMIT_MODE}" "${OUTPUT_PATH}" "${INPUT_PATH}"
    RESULT_VARIABLE command_result
    OUTPUT_VARIABLE command_stdout
    ERROR_VARIABLE command_stderr
)

if(command_result EQUAL 0)
    message(FATAL_ERROR
        "compiler command unexpectedly succeeded with missing toolchain driver\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
endif()

foreach(required_fragment
        "failed to launch process"
        "required toolchain driver"
        "${TOOLCHAIN_CLANG}")
    string(FIND "${command_stderr}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "missing-toolchain diagnostic did not contain required fragment '${required_fragment}'\nstderr:\n${command_stderr}")
    endif()
endforeach()

foreach(unexpected_path "${OUTPUT_PATH}" "${temp_ir_path}" "${log_path}")
    if(EXISTS "${unexpected_path}")
        message(FATAL_ERROR "missing-toolchain native emission left unexpected artifact: ${unexpected_path}")
    endif()
endforeach()
