if(NOT DEFINED COMMAND_PATH)
    message(FATAL_ERROR "COMMAND_PATH is required")
endif()

if(NOT DEFINED CLANG_PATH)
    message(FATAL_ERROR "CLANG_PATH is required")
endif()

if(NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "WORK_DIR is required")
endif()

set(isolated_path "${WORK_DIR}/toolchain-missing-linker-path")
file(MAKE_DIRECTORY "${isolated_path}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "EVIDENT_CLANG=${CLANG_PATH}" "PATH=${isolated_path}" "${COMMAND_PATH}" --check-toolchain
    RESULT_VARIABLE command_result
    OUTPUT_VARIABLE command_stdout
    ERROR_VARIABLE command_stderr
)

if(command_result EQUAL 0)
    message(FATAL_ERROR
        "toolchain check unexpectedly succeeded without lld-link on PATH\nstdout:\n${command_stdout}\nstderr:\n${command_stderr}")
endif()

set(actual_stdout "${command_stdout}")
string(REPLACE "\r\n" "\n" actual_stdout "${actual_stdout}")
string(REPLACE "\r" "\n" actual_stdout "${actual_stdout}")
string(REPLACE "\\" "/" actual_stdout "${actual_stdout}")
string(REGEX REPLACE "[\n]+$" "\n" actual_stdout "${actual_stdout}")

file(TO_CMAKE_PATH "${CLANG_PATH}" clang_path_cmake)
string(CONCAT expected_stdout
    "native target: x86_64-pc-windows-msvc\n"
    "supported native target: x86_64-pc-windows-msvc\n"
    "clang driver: ${clang_path_cmake}\n"
    "clang override env: EVIDENT_CLANG\n"
    "linker mode: clang -fuse-ld=lld\n")

if(NOT actual_stdout STREQUAL expected_stdout)
    message(FATAL_ERROR
        "toolchain check stdout mismatch when lld-link is unavailable\nexpected:\n${expected_stdout}\nactual:\n${actual_stdout}")
endif()

set(actual_stderr "${command_stderr}")
string(REPLACE "\r\n" "\n" actual_stderr "${actual_stderr}")
string(REPLACE "\r" "\n" actual_stderr "${actual_stderr}")
string(REPLACE "\\" "/" actual_stderr "${actual_stderr}")
string(REGEX REPLACE "[\n]+$" "\n" actual_stderr "${actual_stderr}")

foreach(required_fragment IN ITEMS
    "toolchain check failed:"
    "failed to launch process"
    "lld-link"
    "required linker driver: lld-link"
    "Install LLVM lld-link and ensure it is available on PATH.")
    string(FIND "${actual_stderr}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "toolchain check stderr did not contain required fragment '${required_fragment}'\nstderr:\n${actual_stderr}")
    endif()
endforeach()

string(FIND "${actual_stderr}" "required toolchain driver" wrong_driver_index)
if(NOT wrong_driver_index EQUAL -1)
    message(FATAL_ERROR
        "toolchain check reported the clang driver as missing instead of lld-link\nstderr:\n${actual_stderr}")
endif()
