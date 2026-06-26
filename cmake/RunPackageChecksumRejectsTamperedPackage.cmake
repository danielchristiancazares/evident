if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" test_suffix)
set(work_dir "${BUILD_DIR}/package-checksum-tampered-${test_suffix}")
set(package_path "${work_dir}/evident-tampered-windows-x64.zip")
set(checksum_path "${package_path}.sha256")
set(non_zip_package_path "${work_dir}/evident-tampered-windows-x64.txt")
set(non_zip_checksum_path "${non_zip_package_path}.sha256")
set(writer_misplaced_checksum_path "${work_dir}/misplaced-writer-output.sha256")

file(MAKE_DIRECTORY "${work_dir}")
file(WRITE "${package_path}" "original package bytes\n")
get_filename_component(package_name "${package_path}" NAME)

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DPACKAGE_PATH=${package_path}"
        "-DOUTPUT_PATH=${writer_misplaced_checksum_path}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/WritePackageSha256.cmake"
    RESULT_VARIABLE writer_misplaced_result
    OUTPUT_VARIABLE writer_misplaced_stdout
    ERROR_VARIABLE writer_misplaced_stderr
)

if(writer_misplaced_result EQUAL 0)
    message(FATAL_ERROR
        "package checksum writer unexpectedly accepted a misplaced checksum output path\nstdout:\n${writer_misplaced_stdout}\nstderr:\n${writer_misplaced_stderr}")
endif()

set(actual_error "${writer_misplaced_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")

foreach(required_fragment IN ITEMS
    "package checksum writer output path must be the package path plus .sha256"
    "${checksum_path}"
    "${writer_misplaced_checksum_path}")
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "misplaced checksum writer output error did not contain required fragment '${required_fragment}'\nstdout:\n${writer_misplaced_stdout}\nstderr:\n${writer_misplaced_stderr}")
    endif()
endforeach()

file(WRITE "${non_zip_package_path}" "not a zip package\n")
file(SHA256 "${non_zip_package_path}" non_zip_sha256)
get_filename_component(non_zip_package_name "${non_zip_package_path}" NAME)
file(WRITE "${non_zip_checksum_path}" "${non_zip_sha256}  ${non_zip_package_name}\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DPACKAGE_PATH=${non_zip_package_path}"
        "-DOUTPUT_PATH=${non_zip_checksum_path}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/WritePackageSha256.cmake"
    RESULT_VARIABLE writer_non_zip_result
    OUTPUT_VARIABLE writer_non_zip_stdout
    ERROR_VARIABLE writer_non_zip_stderr
)

if(writer_non_zip_result EQUAL 0)
    message(FATAL_ERROR
        "package checksum writer unexpectedly accepted a non-ZIP package path\nstdout:\n${writer_non_zip_stdout}\nstderr:\n${writer_non_zip_stderr}")
endif()

set(actual_error "${writer_non_zip_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")

foreach(required_fragment IN ITEMS
    "package checksum writer package path must name a .zip archive:"
    "${non_zip_package_path}")
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "non-zip package checksum writer error did not contain required fragment '${required_fragment}'\nstdout:\n${writer_non_zip_stdout}\nstderr:\n${writer_non_zip_stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DPACKAGE_PATH=${non_zip_package_path}"
        "-DCHECKSUM_PATH=${non_zip_checksum_path}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertPackageChecksum.cmake"
    RESULT_VARIABLE non_zip_result
    OUTPUT_VARIABLE non_zip_stdout
    ERROR_VARIABLE non_zip_stderr
)

if(non_zip_result EQUAL 0)
    message(FATAL_ERROR
        "package checksum validation unexpectedly accepted a non-ZIP package path\nstdout:\n${non_zip_stdout}\nstderr:\n${non_zip_stderr}")
endif()

set(actual_error "${non_zip_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")

foreach(required_fragment IN ITEMS
    "package checksum package path must name a .zip archive:"
    "${non_zip_package_path}")
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "non-zip package checksum error did not contain required fragment '${required_fragment}'\nstdout:\n${non_zip_stdout}\nstderr:\n${non_zip_stderr}")
    endif()
endforeach()

file(SHA256 "${package_path}" original_sha256)
set(original_line "${original_sha256}  ${package_name}")
file(WRITE "${checksum_path}" "${original_line}\n")

file(APPEND "${package_path}" "tampered package bytes\n")
file(SHA256 "${package_path}" tampered_sha256)
set(tampered_line "${tampered_sha256}  ${package_name}")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DPACKAGE_PATH=${package_path}"
        "-DCHECKSUM_PATH=${checksum_path}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertPackageChecksum.cmake"
    RESULT_VARIABLE validation_result
    OUTPUT_VARIABLE validation_stdout
    ERROR_VARIABLE validation_stderr
)

if(validation_result EQUAL 0)
    message(FATAL_ERROR
        "package checksum validation unexpectedly accepted a tampered package\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
endif()

set(actual_error "${validation_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")

foreach(required_fragment IN ITEMS
    "package checksum mismatch"
    "expected: ${tampered_sha256}"
    "actual: ${original_sha256}"
    "${package_name}")
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "tampered package checksum error did not contain required fragment '${required_fragment}'\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
    endif()
endforeach()

set(wrong_package_name "evident-other-windows-x64.zip")
set(wrong_name_line "${tampered_sha256}  ${wrong_package_name}")
file(WRITE "${checksum_path}" "${wrong_name_line}\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DPACKAGE_PATH=${package_path}"
        "-DCHECKSUM_PATH=${checksum_path}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertPackageChecksum.cmake"
    RESULT_VARIABLE wrong_name_result
    OUTPUT_VARIABLE wrong_name_stdout
    ERROR_VARIABLE wrong_name_stderr
)

if(wrong_name_result EQUAL 0)
    message(FATAL_ERROR
        "package checksum validation unexpectedly accepted a checksum sidecar naming a different package\nstdout:\n${wrong_name_stdout}\nstderr:\n${wrong_name_stderr}")
endif()

set(actual_error "${wrong_name_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")

foreach(required_fragment IN ITEMS
    "package checksum mismatch"
    "expected: ${tampered_sha256}"
    "${package_name}"
    "actual: ${tampered_sha256}"
    "${wrong_package_name}")
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "wrong-package-name checksum error did not contain required fragment '${required_fragment}'\nstdout:\n${wrong_name_stdout}\nstderr:\n${wrong_name_stderr}")
    endif()
endforeach()

set(misplaced_checksum_path "${work_dir}/misplaced-${package_name}.sha256")
file(WRITE "${checksum_path}" "${tampered_line}\n")
file(WRITE "${misplaced_checksum_path}" "${tampered_line}\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DPACKAGE_PATH=${package_path}"
        "-DCHECKSUM_PATH=${misplaced_checksum_path}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertPackageChecksum.cmake"
    RESULT_VARIABLE misplaced_result
    OUTPUT_VARIABLE misplaced_stdout
    ERROR_VARIABLE misplaced_stderr
)

if(misplaced_result EQUAL 0)
    message(FATAL_ERROR
        "package checksum validation unexpectedly accepted a misplaced checksum sidecar\nstdout:\n${misplaced_stdout}\nstderr:\n${misplaced_stderr}")
endif()

set(actual_error "${misplaced_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")

foreach(required_fragment IN ITEMS
    "package checksum path must be the package path plus .sha256"
    "${checksum_path}"
    "${misplaced_checksum_path}")
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "misplaced-checksum-path error did not contain required fragment '${required_fragment}'\nstdout:\n${misplaced_stdout}\nstderr:\n${misplaced_stderr}")
    endif()
endforeach()

function(assert_noncanonical_sidecar_rejected case_name checksum_text)
    file(WRITE "${checksum_path}" "${checksum_text}")

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DPACKAGE_PATH=${package_path}"
            "-DCHECKSUM_PATH=${checksum_path}"
            "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertPackageChecksum.cmake"
        RESULT_VARIABLE validation_result
        OUTPUT_VARIABLE validation_stdout
        ERROR_VARIABLE validation_stderr
    )

    if(validation_result EQUAL 0)
        message(FATAL_ERROR
            "${case_name} checksum validation unexpectedly accepted non-canonical sidecar formatting\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
    endif()

    set(actual_error "${validation_stderr}")
    string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
    string(REPLACE "\r" "\n" actual_error "${actual_error}")

    foreach(required_fragment IN ITEMS
        "package checksum mismatch"
        "expected:"
        "actual:"
        "${tampered_sha256}"
        "${package_name}")
        string(FIND "${actual_error}" "${required_fragment}" fragment_index)
        if(fragment_index EQUAL -1)
            message(FATAL_ERROR
                "${case_name} checksum error did not contain required fragment '${required_fragment}'\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
        endif()
    endforeach()
endfunction()

set(missing_final_newline_text "${tampered_line}")
assert_noncanonical_sidecar_rejected("missing-final-newline" "${missing_final_newline_text}")

set(extra_trailing_newline_text "${tampered_line}\n\n")
assert_noncanonical_sidecar_rejected("extra-trailing-newline" "${extra_trailing_newline_text}")
