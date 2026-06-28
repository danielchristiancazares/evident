if(NOT DEFINED COMMAND_PATH)
    message(FATAL_ERROR "COMMAND_PATH is required")
endif()

if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

set(work_dir "${BUILD_DIR}/invalid-utf8-source")
file(MAKE_DIRECTORY "${work_dir}")

string(ASCII 192 invalid_lead_byte)
set(input_path "${work_dir}/invalid_utf8.evd")
file(WRITE "${input_path}" "${invalid_lead_byte}")

execute_process(
    COMMAND "${COMMAND_PATH}" "${input_path}"
    RESULT_VARIABLE command_result
    OUTPUT_VARIABLE command_stdout
    ERROR_VARIABLE command_stderr
)

if(command_result EQUAL 0)
    message(FATAL_ERROR
        "compiler command unexpectedly accepted ill-formed UTF-8 source\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
endif()

if(NOT command_stdout STREQUAL "")
    message(FATAL_ERROR
        "compiler command wrote unexpected stdout for ill-formed UTF-8 source\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
endif()

set(actual_output "${command_stderr}")
string(REPLACE "\r\n" "\n" actual_output "${actual_output}")
string(REPLACE "\r" "\n" actual_output "${actual_output}")
string(REPLACE "\\" "/" actual_output "${actual_output}")
file(TO_CMAKE_PATH "${input_path}" input_path_cmake)
file(TO_NATIVE_PATH "${input_path}" input_path_native)
string(REPLACE "${input_path}" "<input>" actual_output "${actual_output}")
string(REPLACE "${input_path_cmake}" "<input>" actual_output "${actual_output}")
string(REPLACE "${input_path_native}" "<input>" actual_output "${actual_output}")
string(REGEX REPLACE "[\n]+$" "\n" actual_output "${actual_output}")

set(expected_output "source file is not well-formed UTF-8 at byte offset 0: <input>\n")

if(NOT actual_output STREQUAL expected_output)
    message(FATAL_ERROR
        "ill-formed UTF-8 source diagnostic mismatch\nexpected:\n${expected_output}\nactual:\n${actual_output}")
endif()
