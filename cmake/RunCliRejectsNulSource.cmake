if(NOT DEFINED COMMAND_PATH)
    message(FATAL_ERROR "COMMAND_PATH is required")
endif()

if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

find_program(POWERSHELL_COMMAND NAMES pwsh powershell powershell.exe)
if(NOT POWERSHELL_COMMAND)
    message(FATAL_ERROR "PowerShell is required to write a NUL-byte source fixture")
endif()

set(work_dir "${BUILD_DIR}/nul-source")
file(MAKE_DIRECTORY "${work_dir}")

set(input_path "${work_dir}/nul_source.evd")
set(writer_path "${work_dir}/write_nul_source.ps1")
file(WRITE "${writer_path}" "param([string]$Path)\n[System.IO.File]::WriteAllBytes($Path, [byte[]](0))\n")
execute_process(
    COMMAND "${POWERSHELL_COMMAND}" -NoProfile -ExecutionPolicy Bypass -File "${writer_path}" "${input_path}"
    RESULT_VARIABLE write_result
    OUTPUT_VARIABLE write_stdout
    ERROR_VARIABLE write_stderr
)

if(NOT write_result EQUAL 0)
    message(FATAL_ERROR
        "failed to write NUL-byte source fixture\nstdout:\n${write_stdout}\nstderr:\n${write_stderr}")
endif()

execute_process(
    COMMAND "${COMMAND_PATH}" "${input_path}"
    RESULT_VARIABLE command_result
    OUTPUT_VARIABLE command_stdout
    ERROR_VARIABLE command_stderr
)

if(command_result EQUAL 0)
    message(FATAL_ERROR
        "compiler command unexpectedly accepted source containing U+0000\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
endif()

if(NOT command_stdout STREQUAL "")
    message(FATAL_ERROR
        "compiler command wrote unexpected stdout for source containing U+0000\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
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

set(expected_output "source file contains U+0000 at byte offset 0: <input>\n")

if(NOT actual_output STREQUAL expected_output)
    message(FATAL_ERROR
        "U+0000 source diagnostic mismatch\nexpected:\n${expected_output}\nactual:\n${actual_output}")
endif()
