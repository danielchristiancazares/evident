if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" test_suffix)
set(work_dir "${BUILD_DIR}/package-zip-missing-entry-${test_suffix}")
set(package_root "evident-missing-entry-windows-x64")
set(package_path "${work_dir}/${package_root}.zip")

file(MAKE_DIRECTORY
    "${work_dir}/${package_root}/bin"
    "${work_dir}/${package_root}/share/doc/evident_compiler"
)
file(WRITE "${work_dir}/${package_root}/bin/evidc.exe" "synthetic executable placeholder\n")
file(WRITE "${work_dir}/${package_root}/share/doc/evident_compiler/README.md" "synthetic readme\n")

execute_process(
    COMMAND "${CMAKE_COMMAND}" "-E" "chdir" "${work_dir}" "${CMAKE_COMMAND}" "-E" "tar" "cf" "${package_path}" "${package_root}"
    RESULT_VARIABLE package_result
    OUTPUT_VARIABLE package_stdout
    ERROR_VARIABLE package_stderr
)

if(NOT package_result EQUAL 0)
    message(FATAL_ERROR
        "failed to create synthetic package ZIP with exit code ${package_result}\nstdout:\n${package_stdout}\nstderr:\n${package_stderr}")
endif()

set(missing_entry "${package_root}/share/doc/evident_compiler/LICENSE")
set(expected_entries
    "${package_root}/bin/evidc.exe"
    "${package_root}/share/doc/evident_compiler/README.md"
    "${missing_entry}"
)

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DPACKAGE_PATH=${package_path}"
        "-DEXPECTED_ENTRIES=${expected_entries}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertPackageZipListing.cmake"
    RESULT_VARIABLE validation_result
    OUTPUT_VARIABLE validation_stdout
    ERROR_VARIABLE validation_stderr
)

if(validation_result EQUAL 0)
    message(FATAL_ERROR
        "package ZIP listing validation unexpectedly accepted a missing expected entry\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
endif()

set(actual_error "${validation_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")

foreach(required_fragment IN ITEMS
    "expected package entry missing:"
    "${missing_entry}")
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "missing-entry validation error did not contain required fragment '${required_fragment}'\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
    endif()
endforeach()
