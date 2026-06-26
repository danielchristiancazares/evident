if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

if(NOT DEFINED EXPECTED_CTEST_TOTAL)
    message(FATAL_ERROR "EXPECTED_CTEST_TOTAL is required")
endif()

if(NOT EXPECTED_CTEST_TOTAL MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "EXPECTED_CTEST_TOTAL must be a positive decimal test count: ${EXPECTED_CTEST_TOTAL}")
endif()

set(package_name "evident-0.1.0-windows-x64.zip")
set(package_size "12345")
set(package_sha256 "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
set(commit_sha "0123456789abcdef0123456789abcdef01234567")
set(other_commit_sha "fedcba9876543210fedcba9876543210fedcba98")
set(expected_print_toolchain_output
    "native target: x86_64-pc-windows-msvc\nsupported native target: x86_64-pc-windows-msvc\nclang driver: clang\nclang override env: EVIDENT_CLANG\nlinker mode: clang -fuse-ld=lld\n"
)
set(expected_check_toolchain_output
    "${expected_print_toolchain_output}clang version: clang version synthetic\nlld-link version: LLD synthetic\n"
)
set(print_toolchain_section_text
    "[evidc --print-toolchain]\n${expected_print_toolchain_output}"
)
set(check_toolchain_section_text
    "[evidc --check-toolchain]\n${expected_check_toolchain_output}"
)
set(print_toolchain_without_supported_target_output
    "native target: x86_64-pc-windows-msvc\nclang driver: clang\nclang override env: EVIDENT_CLANG\nlinker mode: clang -fuse-ld=lld\n"
)

function(write_release_evidence path include_toolchain_check ctest_total)
    file(WRITE "${path}" "Evident Windows x64 release evidence\n")
    file(APPEND "${path}" "commit: ${commit_sha}\n")
    file(APPEND "${path}" "runner: windows-2022\n")
    file(APPEND "${path}" "runner image os: win22\n")
    file(APPEND "${path}" "runner image version: synthetic.image.version\n")
    file(APPEND "${path}" "native target: x86_64-pc-windows-msvc\n")
    file(APPEND "${path}" "package preset: windows-x64-ninja-package-checksum\n")
    file(APPEND "${path}" "\n")
    file(APPEND "${path}" "[release source tree audit]\nrelease source tree audit: passed\n")
    file(APPEND "${path}" "\n")
    file(APPEND "${path}" "[validated commands]\n")
    file(APPEND "${path}" "cmake --preset windows-x64-ninja\n")
    file(APPEND "${path}" "cmake --build --preset windows-x64-ninja\n")
    file(APPEND "${path}" "ctest --preset windows-x64-ninja\n")
    file(APPEND "${path}" "cmake --build --preset windows-x64-ninja-package-checksum\n")
    file(APPEND "${path}" "\n")
    file(APPEND "${path}" "[ctest output]\n100% tests passed, 0 tests failed out of ${ctest_total}\n")
    file(APPEND "${path}" "\n")
    file(APPEND "${path}" "[vswhere]\ninstallationPath: C:\\synthetic\\Visual Studio\ninstallationVersion: 17.0.synthetic\n")
    file(APPEND "${path}" "\n")
    file(APPEND "${path}" "[where cmake]\nC:\\synthetic\\cmake.exe\n")
    file(APPEND "${path}" "\n")
    file(APPEND "${path}" "[cmake --version]\ncmake version synthetic\n")
    file(APPEND "${path}" "\n")
    file(APPEND "${path}" "[where ninja]\nC:\\synthetic\\ninja.exe\n")
    file(APPEND "${path}" "\n")
    file(APPEND "${path}" "[ninja --version]\n1.0.synthetic\n")
    file(APPEND "${path}" "\n")
    file(APPEND "${path}" "[where cl]\nC:\\synthetic\\cl.exe\n")
    file(APPEND "${path}" "\n")
    file(APPEND "${path}" "[cl]\nMicrosoft (R) C/C++ Optimizing Compiler Version synthetic for x64\n")
    file(APPEND "${path}" "\n")
    file(APPEND "${path}" "[where clang]\nC:\\synthetic\\clang.exe\n")
    file(APPEND "${path}" "\n")
    file(APPEND "${path}" "[clang --version]\nclang version synthetic\n")
    file(APPEND "${path}" "\n")
    file(APPEND "${path}" "[where lld-link]\nC:\\synthetic\\lld-link.exe\n")
    file(APPEND "${path}" "\n")
    file(APPEND "${path}" "[lld-link --version]\nLLD synthetic\n")
    file(APPEND "${path}" "\n")
    file(APPEND "${path}" "[evidc --version]\nevidc 0.1.0\n")
    file(APPEND "${path}" "\n")
    file(APPEND "${path}" "${print_toolchain_section_text}")
    file(APPEND "${path}" "\n")
    if(include_toolchain_check)
        file(APPEND "${path}" "${check_toolchain_section_text}")
        file(APPEND "${path}" "\n")
    endif()
    file(APPEND "${path}" "[release ZIP]\n")
    file(APPEND "${path}" "file: ${package_name}\n")
    file(APPEND "${path}" "bytes: ${package_size}\n")
    file(APPEND "${path}" "sha256: ${package_sha256}\n")
endfunction()

string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" test_suffix)
set(work_dir "${BUILD_DIR}/release-evidence-${test_suffix}")
set(valid_evidence_path "${work_dir}/valid-evident-release-evidence.txt")
set(missing_source_tree_audit_evidence_path "${work_dir}/missing-source-tree-audit-evident-release-evidence.txt")
set(missing_section_evidence_path "${work_dir}/missing-section-evident-release-evidence.txt")
set(out_of_order_section_evidence_path "${work_dir}/out-of-order-section-evident-release-evidence.txt")
set(unexpected_section_evidence_path "${work_dir}/unexpected-section-evident-release-evidence.txt")
set(unexpected_command_evidence_path "${work_dir}/unexpected-command-evident-release-evidence.txt")
set(stale_ctest_evidence_path "${work_dir}/stale-ctest-evident-release-evidence.txt")
set(misplaced_ctest_summary_evidence_path "${work_dir}/misplaced-ctest-summary-evident-release-evidence.txt")
set(empty_runner_image_evidence_path "${work_dir}/empty-runner-image-evident-release-evidence.txt")
set(non_windows_runner_image_os_evidence_path "${work_dir}/non-windows-runner-image-os-evident-release-evidence.txt")
set(relative_vswhere_path_evidence_path "${work_dir}/relative-vswhere-path-evident-release-evidence.txt")
set(empty_vswhere_version_evidence_path "${work_dir}/empty-vswhere-version-evident-release-evidence.txt")
set(non_x64_msvc_evidence_path "${work_dir}/non-x64-msvc-evident-release-evidence.txt")
set(empty_tool_version_evidence_path "${work_dir}/empty-tool-version-evident-release-evidence.txt")
set(ambiguous_cmake_version_evidence_path "${work_dir}/ambiguous-cmake-version-evident-release-evidence.txt")
set(ambiguous_ninja_version_evidence_path "${work_dir}/ambiguous-ninja-version-evident-release-evidence.txt")
set(ambiguous_clang_version_evidence_path "${work_dir}/ambiguous-clang-version-evident-release-evidence.txt")
set(ambiguous_lld_link_version_evidence_path "${work_dir}/ambiguous-lld-link-version-evident-release-evidence.txt")
set(empty_toolchain_output_evidence_path "${work_dir}/empty-toolchain-output-evident-release-evidence.txt")
set(mismatched_evidc_version_evidence_path "${work_dir}/mismatched-evidc-version-evident-release-evidence.txt")
set(missing_print_toolchain_supported_target_evidence_path "${work_dir}/missing-print-toolchain-supported-target-evident-release-evidence.txt")
set(ambiguous_print_toolchain_clang_driver_evidence_path "${work_dir}/ambiguous-print-toolchain-clang-driver-evident-release-evidence.txt")
set(legacy_check_toolchain_summary_evidence_path "${work_dir}/legacy-check-toolchain-summary-evident-release-evidence.txt")
set(empty_check_toolchain_clang_version_evidence_path "${work_dir}/empty-check-toolchain-clang-version-evident-release-evidence.txt")
set(ambiguous_check_toolchain_clang_version_evidence_path "${work_dir}/ambiguous-check-toolchain-clang-version-evident-release-evidence.txt")
set(ambiguous_check_toolchain_lld_link_version_evidence_path "${work_dir}/ambiguous-check-toolchain-lld-link-version-evident-release-evidence.txt")
set(mismatched_native_target_evidence_path "${work_dir}/mismatched-native-target-evident-release-evidence.txt")
set(relative_tool_path_evidence_path "${work_dir}/relative-tool-path-evident-release-evidence.txt")
set(mixed_tool_path_evidence_path "${work_dir}/mixed-tool-path-evident-release-evidence.txt")
set(invalid_commit_evidence_path "${work_dir}/invalid-commit-evident-release-evidence.txt")
set(mismatched_expected_commit_evidence_path "${work_dir}/mismatched-expected-commit-evident-release-evidence.txt")
set(duplicate_commit_evidence_path "${work_dir}/duplicate-commit-evident-release-evidence.txt")
set(conflicting_zip_sha_evidence_path "${work_dir}/conflicting-zip-sha-evident-release-evidence.txt")
set(duplicated_package_details_before_zip_evidence_path "${work_dir}/duplicated-package-details-before-zip-evident-release-evidence.txt")
set(misplaced_package_details_evidence_path "${work_dir}/misplaced-package-details-evident-release-evidence.txt")
set(invalid_package_name_evidence_path "${work_dir}/invalid-package-name-evident-release-evidence.txt")
set(invalid_package_size_evidence_path "${work_dir}/invalid-package-size-evident-release-evidence.txt")
set(invalid_package_sha_evidence_path "${work_dir}/invalid-package-sha-evident-release-evidence.txt")
set(missing_package_details_evidence_path "${work_dir}/missing-package-details-evident-release-evidence.txt")
file(MAKE_DIRECTORY "${work_dir}")

write_release_evidence("${valid_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${valid_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DEXPECTED_COMMIT_SHA=${commit_sha}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE valid_result
    OUTPUT_VARIABLE valid_stdout
    ERROR_VARIABLE valid_stderr
)

if(NOT valid_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation rejected complete synthetic evidence with exit code ${valid_result}\nstdout:\n${valid_stdout}\nstderr:\n${valid_stderr}")
endif()

function(assert_release_evidence_rejected evidence_path accepted_message)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DEVIDENCE_PATH=${evidence_path}"
            "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
            "-DRELEASE_PACKAGE_NAME=${package_name}"
            "-DRELEASE_PACKAGE_SIZE=${package_size}"
            "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
            "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
        RESULT_VARIABLE rejected_result
        OUTPUT_VARIABLE rejected_stdout
        ERROR_VARIABLE rejected_stderr
    )

    if(rejected_result EQUAL 0)
        message(FATAL_ERROR
            "${accepted_message}\nstdout:\n${rejected_stdout}\nstderr:\n${rejected_stderr}")
    endif()

    set(actual_error "${rejected_stderr}")
    string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
    string(REPLACE "\r" "\n" actual_error "${actual_error}")
    foreach(required_fragment IN LISTS ARGN)
        string(FIND "${actual_error}" "${required_fragment}" fragment_index)
        if(fragment_index EQUAL -1)
            message(FATAL_ERROR
                "release evidence error for ${evidence_path} did not contain required fragment '${required_fragment}'\nstdout:\n${rejected_stdout}\nstderr:\n${rejected_stderr}")
        endif()
    endforeach()
endfunction()

write_release_evidence("${missing_source_tree_audit_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${missing_source_tree_audit_evidence_path}" missing_source_tree_audit_evidence)
string(REPLACE
    "[release source tree audit]\nrelease source tree audit: passed\n\n"
    ""
    missing_source_tree_audit_evidence
    "${missing_source_tree_audit_evidence}"
)
file(WRITE "${missing_source_tree_audit_evidence_path}" "${missing_source_tree_audit_evidence}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${missing_source_tree_audit_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE missing_source_tree_audit_result
    OUTPUT_VARIABLE missing_source_tree_audit_stdout
    ERROR_VARIABLE missing_source_tree_audit_stderr
)

if(missing_source_tree_audit_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted evidence missing the source-tree audit section\nstdout:\n${missing_source_tree_audit_stdout}\nstderr:\n${missing_source_tree_audit_stderr}")
endif()

set(actual_error "${missing_source_tree_audit_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence is missing expected text"
    "[release source tree audit]"
    "missing-source-tree-audit-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "missing-source-tree-audit release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${missing_source_tree_audit_stdout}\nstderr:\n${missing_source_tree_audit_stderr}")
    endif()
endforeach()

write_release_evidence("${missing_section_evidence_path}" FALSE "${EXPECTED_CTEST_TOTAL}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${missing_section_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE missing_result
    OUTPUT_VARIABLE missing_stdout
    ERROR_VARIABLE missing_stderr
)

if(missing_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted evidence missing the toolchain-check section\nstdout:\n${missing_stdout}\nstderr:\n${missing_stderr}")
endif()

set(actual_error "${missing_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
set(required_fragment "release evidence is missing expected text '[evidc --check-toolchain]'")
string(FIND "${actual_error}" "${required_fragment}" fragment_index)
if(fragment_index EQUAL -1)
    message(FATAL_ERROR
        "missing-section release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${missing_stdout}\nstderr:\n${missing_stderr}")
endif()

write_release_evidence("${out_of_order_section_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${out_of_order_section_evidence_path}" out_of_order_section_evidence)
string(REPLACE
    "[validated commands]"
    "[temporary validated commands]"
    out_of_order_section_evidence
    "${out_of_order_section_evidence}"
)
string(REPLACE
    "[ctest output]"
    "[validated commands]"
    out_of_order_section_evidence
    "${out_of_order_section_evidence}"
)
string(REPLACE
    "[temporary validated commands]"
    "[ctest output]"
    out_of_order_section_evidence
    "${out_of_order_section_evidence}"
)
file(WRITE "${out_of_order_section_evidence_path}" "${out_of_order_section_evidence}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${out_of_order_section_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE out_of_order_section_result
    OUTPUT_VARIABLE out_of_order_section_stdout
    ERROR_VARIABLE out_of_order_section_stderr
)

if(out_of_order_section_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted out-of-order sections\nstdout:\n${out_of_order_section_stdout}\nstderr:\n${out_of_order_section_stderr}")
endif()

set(actual_error "${out_of_order_section_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence sections must appear in canonical order"
    "'[ctest output]'"
    "appears before '[validated commands]'"
    "out-of-order-section-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "out-of-order-section release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${out_of_order_section_stdout}\nstderr:\n${out_of_order_section_stderr}")
    endif()
endforeach()

write_release_evidence("${unexpected_section_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${unexpected_section_evidence_path}" unexpected_section_evidence)
string(REPLACE
    "[clang --version]\nclang version synthetic\n\n[where lld-link]"
    "[clang --version]\nclang version synthetic\n\n[unexpected release note]\nextra ambiguous release output\n\n[where lld-link]"
    unexpected_section_evidence
    "${unexpected_section_evidence}"
)
file(WRITE "${unexpected_section_evidence_path}" "${unexpected_section_evidence}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${unexpected_section_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE unexpected_section_result
    OUTPUT_VARIABLE unexpected_section_stdout
    ERROR_VARIABLE unexpected_section_stderr
)

if(unexpected_section_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted an unexpected section header\nstdout:\n${unexpected_section_stdout}\nstderr:\n${unexpected_section_stderr}")
endif()

set(actual_error "${unexpected_section_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence contains unexpected section header"
    "unexpected release"
    "unexpected-section-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "unexpected-section release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${unexpected_section_stdout}\nstderr:\n${unexpected_section_stderr}")
    endif()
endforeach()

write_release_evidence("${unexpected_command_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${unexpected_command_evidence_path}" unexpected_command_evidence)
string(REPLACE
    "ctest --preset windows-x64-ninja\n"
    "ctest --preset windows-x64-ninja\ncmake --build --preset windows-x64-ninja-install\n"
    unexpected_command_evidence
    "${unexpected_command_evidence}"
)
file(WRITE "${unexpected_command_evidence_path}" "${unexpected_command_evidence}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${unexpected_command_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE unexpected_command_result
    OUTPUT_VARIABLE unexpected_command_stdout
    ERROR_VARIABLE unexpected_command_stderr
)

if(unexpected_command_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted an extra validated command\nstdout:\n${unexpected_command_stdout}\nstderr:\n${unexpected_command_stderr}")
endif()

set(actual_error "${unexpected_command_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence [validated commands] section must list exactly the"
    "supported release commands"
    "unexpected-command-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "unexpected-command release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${unexpected_command_stdout}\nstderr:\n${unexpected_command_stderr}")
    endif()
endforeach()

math(EXPR stale_ctest_total "${EXPECTED_CTEST_TOTAL} - 1")
write_release_evidence("${stale_ctest_evidence_path}" TRUE "${stale_ctest_total}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${stale_ctest_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE stale_ctest_result
    OUTPUT_VARIABLE stale_ctest_stdout
    ERROR_VARIABLE stale_ctest_stderr
)

if(stale_ctest_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted a stale CTest pass summary\nstdout:\n${stale_ctest_stdout}\nstderr:\n${stale_ctest_stderr}")
endif()

set(actual_error "${stale_ctest_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence [ctest output] section is missing expected CTest"
    "summary '100% tests passed"
    "failed out of ${EXPECTED_CTEST_TOTAL}"
    "stale-ctest-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "stale-ctest release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${stale_ctest_stdout}\nstderr:\n${stale_ctest_stderr}")
    endif()
endforeach()

write_release_evidence("${misplaced_ctest_summary_evidence_path}" TRUE "${stale_ctest_total}")
file(READ "${misplaced_ctest_summary_evidence_path}" misplaced_ctest_summary_evidence)
string(REPLACE
    "[ninja --version]\n1.0.synthetic\n"
    "[ninja --version]\n1.0.synthetic\n100% tests passed, 0 tests failed out of ${EXPECTED_CTEST_TOTAL}\n"
    misplaced_ctest_summary_evidence
    "${misplaced_ctest_summary_evidence}"
)
file(WRITE "${misplaced_ctest_summary_evidence_path}" "${misplaced_ctest_summary_evidence}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${misplaced_ctest_summary_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE misplaced_ctest_summary_result
    OUTPUT_VARIABLE misplaced_ctest_summary_stdout
    ERROR_VARIABLE misplaced_ctest_summary_stderr
)

if(misplaced_ctest_summary_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted a misplaced CTest pass summary\nstdout:\n${misplaced_ctest_summary_stdout}\nstderr:\n${misplaced_ctest_summary_stderr}")
endif()

set(actual_error "${misplaced_ctest_summary_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence [ctest output] section is missing expected CTest"
    "summary '100% tests passed"
    "failed out of ${EXPECTED_CTEST_TOTAL}"
    "misplaced-ctest-summary-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "misplaced-ctest-summary release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${misplaced_ctest_summary_stdout}\nstderr:\n${misplaced_ctest_summary_stderr}")
    endif()
endforeach()

write_release_evidence("${empty_runner_image_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${empty_runner_image_evidence_path}" empty_runner_image_evidence)
string(REPLACE
    "runner image version: synthetic.image.version\n"
    "runner image version:\n"
    empty_runner_image_evidence
    "${empty_runner_image_evidence}"
)
file(WRITE "${empty_runner_image_evidence_path}" "${empty_runner_image_evidence}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${empty_runner_image_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE empty_runner_image_result
    OUTPUT_VARIABLE empty_runner_image_stdout
    ERROR_VARIABLE empty_runner_image_stderr
)

if(empty_runner_image_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted an empty runner image version\nstdout:\n${empty_runner_image_stdout}\nstderr:\n${empty_runner_image_stderr}")
endif()

set(actual_error "${empty_runner_image_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence top provenance runner image version line must contain a"
    "non-empty value after"
    "runner image version:"
    "empty-runner-image-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "empty-runner-image release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${empty_runner_image_stdout}\nstderr:\n${empty_runner_image_stderr}")
    endif()
endforeach()

write_release_evidence("${non_windows_runner_image_os_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${non_windows_runner_image_os_evidence_path}" non_windows_runner_image_os_evidence)
string(REPLACE
    "runner image os: win22\n"
    "runner image os: ubuntu22\n"
    non_windows_runner_image_os_evidence
    "${non_windows_runner_image_os_evidence}"
)
file(WRITE "${non_windows_runner_image_os_evidence_path}" "${non_windows_runner_image_os_evidence}")
assert_release_evidence_rejected(
    "${non_windows_runner_image_os_evidence_path}"
    "release evidence validation unexpectedly accepted a runner image OS that does not identify Windows"
    "release evidence top provenance runner image OS line"
    "invalid"
    "ubuntu22"
    "runner image os:"
    "non-windows-runner-image-os-evident-release-evidence.txt"
)

write_release_evidence("${relative_vswhere_path_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${relative_vswhere_path_evidence_path}" relative_vswhere_path_evidence)
string(REPLACE
    "[vswhere]\ninstallationPath: C:\\synthetic\\Visual Studio\n"
    "[vswhere]\ninstallationPath: synthetic\\Visual Studio\n"
    relative_vswhere_path_evidence
    "${relative_vswhere_path_evidence}"
)
file(WRITE "${relative_vswhere_path_evidence_path}" "${relative_vswhere_path_evidence}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${relative_vswhere_path_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE relative_vswhere_path_result
    OUTPUT_VARIABLE relative_vswhere_path_stdout
    ERROR_VARIABLE relative_vswhere_path_stderr
)

if(relative_vswhere_path_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted a relative Visual Studio installation path\nstdout:\n${relative_vswhere_path_stdout}\nstderr:\n${relative_vswhere_path_stderr}")
endif()

set(actual_error "${relative_vswhere_path_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence [vswhere] Visual Studio installation path has invalid"
    "value 'synthetic\\Visual Studio'"
    "synthetic\\Visual Studio"
    "relative-vswhere-path-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "relative-vswhere-path release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${relative_vswhere_path_stdout}\nstderr:\n${relative_vswhere_path_stderr}")
    endif()
endforeach()

write_release_evidence("${empty_vswhere_version_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${empty_vswhere_version_evidence_path}" empty_vswhere_version_evidence)
string(REPLACE
    "installationVersion: 17.0.synthetic\n"
    "installationVersion:\n"
    empty_vswhere_version_evidence
    "${empty_vswhere_version_evidence}"
)
file(WRITE "${empty_vswhere_version_evidence_path}" "${empty_vswhere_version_evidence}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${empty_vswhere_version_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE empty_vswhere_version_result
    OUTPUT_VARIABLE empty_vswhere_version_stdout
    ERROR_VARIABLE empty_vswhere_version_stderr
)

if(empty_vswhere_version_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted an empty Visual Studio installation version\nstdout:\n${empty_vswhere_version_stdout}\nstderr:\n${empty_vswhere_version_stderr}")
endif()

set(actual_error "${empty_vswhere_version_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence [vswhere] Visual Studio installation version must contain"
    "a non-empty value after"
    "installationVersion:"
    "empty-vswhere-version-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "empty-vswhere-version release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${empty_vswhere_version_stdout}\nstderr:\n${empty_vswhere_version_stderr}")
    endif()
endforeach()

write_release_evidence("${non_x64_msvc_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${non_x64_msvc_evidence_path}" non_x64_msvc_evidence)
string(REPLACE
    "Microsoft (R) C/C++ Optimizing Compiler Version synthetic for x64"
    "Microsoft (R) C/C++ Optimizing Compiler Version synthetic for x86"
    non_x64_msvc_evidence
    "${non_x64_msvc_evidence}"
)
file(WRITE "${non_x64_msvc_evidence_path}" "${non_x64_msvc_evidence}")
assert_release_evidence_rejected(
    "${non_x64_msvc_evidence_path}"
    "release evidence validation unexpectedly accepted an MSVC compiler banner that does not identify the x64 target architecture"
    "release evidence [cl] MSVC compiler target architecture"
    "missing required"
    "for x64"
    "for x86"
    "non-x64-msvc-evident-release-evidence.txt"
)

write_release_evidence("${empty_tool_version_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${empty_tool_version_evidence_path}" empty_tool_version_evidence)
string(REPLACE
    "[clang --version]\nclang version synthetic\n\n[where lld-link]"
    "[clang --version]\n\n[where lld-link]"
    empty_tool_version_evidence
    "${empty_tool_version_evidence}"
)
file(WRITE "${empty_tool_version_evidence_path}" "${empty_tool_version_evidence}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${empty_tool_version_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE empty_tool_version_result
    OUTPUT_VARIABLE empty_tool_version_stdout
    ERROR_VARIABLE empty_tool_version_stderr
)

if(empty_tool_version_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted an empty tool-version output section\nstdout:\n${empty_tool_version_stdout}\nstderr:\n${empty_tool_version_stderr}")
endif()

set(actual_error "${empty_tool_version_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence [clang --version] section must contain"
    "non-empty"
    "empty-tool-version-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "empty-tool-version release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${empty_tool_version_stdout}\nstderr:\n${empty_tool_version_stderr}")
    endif()
endforeach()

write_release_evidence("${ambiguous_cmake_version_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${ambiguous_cmake_version_evidence_path}" ambiguous_cmake_version_evidence)
string(REPLACE
    "[cmake --version]\ncmake version synthetic\n"
    "[cmake --version]\nversion synthetic\n"
    ambiguous_cmake_version_evidence
    "${ambiguous_cmake_version_evidence}"
)
file(WRITE "${ambiguous_cmake_version_evidence_path}" "${ambiguous_cmake_version_evidence}")
assert_release_evidence_rejected(
    "${ambiguous_cmake_version_evidence_path}"
    "release evidence validation unexpectedly accepted CMake version output that does not identify CMake"
    "release evidence [cmake --version] tool version"
    "line starting"
    "cmake version"
    "found 0"
    "ambiguous-cmake-version-evident-release-evidence.txt"
)

write_release_evidence("${ambiguous_ninja_version_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${ambiguous_ninja_version_evidence_path}" ambiguous_ninja_version_evidence)
string(REPLACE
    "[ninja --version]\n1.0.synthetic\n"
    "[ninja --version]\nninja version synthetic\n"
    ambiguous_ninja_version_evidence
    "${ambiguous_ninja_version_evidence}"
)
file(WRITE "${ambiguous_ninja_version_evidence_path}" "${ambiguous_ninja_version_evidence}")
assert_release_evidence_rejected(
    "${ambiguous_ninja_version_evidence_path}"
    "release evidence validation unexpectedly accepted Ninja version output without a numeric version line"
    "release evidence [ninja --version] tool version"
    "line matching"
    "found 0"
    "ambiguous-ninja-version-evident-release-evidence.txt"
)

write_release_evidence("${ambiguous_clang_version_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${ambiguous_clang_version_evidence_path}" ambiguous_clang_version_evidence)
string(REPLACE
    "[clang --version]\nclang version synthetic\n"
    "[clang --version]\nversion synthetic\n"
    ambiguous_clang_version_evidence
    "${ambiguous_clang_version_evidence}"
)
file(WRITE "${ambiguous_clang_version_evidence_path}" "${ambiguous_clang_version_evidence}")
assert_release_evidence_rejected(
    "${ambiguous_clang_version_evidence_path}"
    "release evidence validation unexpectedly accepted clang version output that does not identify clang"
    "release evidence [clang --version] tool version"
    "line starting"
    "clang version"
    "found 0"
    "ambiguous-clang-version-evident-release-evidence.txt"
)

write_release_evidence("${ambiguous_lld_link_version_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${ambiguous_lld_link_version_evidence_path}" ambiguous_lld_link_version_evidence)
string(REPLACE
    "[lld-link --version]\nLLD synthetic\n"
    "[lld-link --version]\nversion synthetic\n"
    ambiguous_lld_link_version_evidence
    "${ambiguous_lld_link_version_evidence}"
)
file(WRITE "${ambiguous_lld_link_version_evidence_path}" "${ambiguous_lld_link_version_evidence}")
assert_release_evidence_rejected(
    "${ambiguous_lld_link_version_evidence_path}"
    "release evidence validation unexpectedly accepted lld-link version output that does not identify LLD"
    "release evidence [lld-link --version] tool version"
    "line starting"
    "LLD"
    "found 0"
    "ambiguous-lld-link-version-evident-release-evidence.txt"
)

write_release_evidence("${empty_toolchain_output_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${empty_toolchain_output_evidence_path}" empty_toolchain_output_evidence)
string(REPLACE
    "${check_toolchain_section_text}\n[release ZIP]"
    "[evidc --check-toolchain]\n\n[release ZIP]"
    empty_toolchain_output_evidence
    "${empty_toolchain_output_evidence}"
)
file(WRITE "${empty_toolchain_output_evidence_path}" "${empty_toolchain_output_evidence}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${empty_toolchain_output_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE empty_toolchain_output_result
    OUTPUT_VARIABLE empty_toolchain_output_stdout
    ERROR_VARIABLE empty_toolchain_output_stderr
)

if(empty_toolchain_output_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted an empty compiler toolchain output section\nstdout:\n${empty_toolchain_output_stdout}\nstderr:\n${empty_toolchain_output_stderr}")
endif()

set(actual_error "${empty_toolchain_output_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence [evidc --check-toolchain] section must contain"
    "non-empty"
    "empty-toolchain-output-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "empty-toolchain-output release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${empty_toolchain_output_stdout}\nstderr:\n${empty_toolchain_output_stderr}")
    endif()
endforeach()

write_release_evidence("${missing_print_toolchain_supported_target_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${missing_print_toolchain_supported_target_evidence_path}" missing_print_toolchain_supported_target_evidence)
string(REPLACE
    "${print_toolchain_section_text}\n[evidc --check-toolchain]"
    "[evidc --print-toolchain]\n${print_toolchain_without_supported_target_output}\n[evidc --check-toolchain]"
    missing_print_toolchain_supported_target_evidence
    "${missing_print_toolchain_supported_target_evidence}"
)
file(WRITE "${missing_print_toolchain_supported_target_evidence_path}" "${missing_print_toolchain_supported_target_evidence}")
assert_release_evidence_rejected(
    "${missing_print_toolchain_supported_target_evidence_path}"
    "release evidence validation unexpectedly accepted compiler toolchain output missing the supported native target"
    "release evidence [evidc --print-toolchain] supported native target"
    "must"
    "supported native target:"
    "x86_64-pc-windows-msvc"
    "found 0"
    "missing-print-toolchain-supported-target-evident-release-evidence.txt"
)

write_release_evidence("${ambiguous_print_toolchain_clang_driver_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${ambiguous_print_toolchain_clang_driver_evidence_path}" ambiguous_print_toolchain_clang_driver_evidence)
string(REPLACE
    "clang driver: clang\n"
    "clang driver: cc\n"
    ambiguous_print_toolchain_clang_driver_evidence
    "${ambiguous_print_toolchain_clang_driver_evidence}"
)
if(ambiguous_print_toolchain_clang_driver_evidence STREQUAL "")
    message(FATAL_ERROR "ambiguous-print-toolchain-clang-driver fixture unexpectedly produced empty evidence text")
endif()
file(WRITE "${ambiguous_print_toolchain_clang_driver_evidence_path}" "${ambiguous_print_toolchain_clang_driver_evidence}")
assert_release_evidence_rejected(
    "${ambiguous_print_toolchain_clang_driver_evidence_path}"
    "release evidence validation unexpectedly accepted compiler toolchain output with a clang driver that does not identify clang"
    "release evidence [evidc --print-toolchain] clang driver"
    "invalid"
    "cc"
    "clang driver:"
    "ambiguous-print-toolchain-clang-driver-evident-release-evidence.txt"
)

write_release_evidence("${legacy_check_toolchain_summary_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${legacy_check_toolchain_summary_evidence_path}" legacy_check_toolchain_summary_evidence)
string(REPLACE
    "${check_toolchain_section_text}"
    "[evidc --check-toolchain]\nclang: ok\nlld-link: ok\n"
    legacy_check_toolchain_summary_evidence
    "${legacy_check_toolchain_summary_evidence}"
)
file(WRITE "${legacy_check_toolchain_summary_evidence_path}" "${legacy_check_toolchain_summary_evidence}")
assert_release_evidence_rejected(
    "${legacy_check_toolchain_summary_evidence_path}"
    "release evidence validation unexpectedly accepted legacy compiler toolchain summary output"
    "release evidence [evidc --check-toolchain] native target"
    "must"
    "native target:"
    "x86_64-pc-windows-msvc"
    "found 0"
    "legacy-check-toolchain-summary-evident-release-evidence.txt"
)

write_release_evidence("${empty_check_toolchain_clang_version_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${empty_check_toolchain_clang_version_evidence_path}" empty_check_toolchain_clang_version_evidence)
string(REPLACE
    "clang version: clang version synthetic\n"
    "clang version:\n"
    empty_check_toolchain_clang_version_evidence
    "${empty_check_toolchain_clang_version_evidence}"
)
file(WRITE "${empty_check_toolchain_clang_version_evidence_path}" "${empty_check_toolchain_clang_version_evidence}")
assert_release_evidence_rejected(
    "${empty_check_toolchain_clang_version_evidence_path}"
    "release evidence validation unexpectedly accepted an empty checked-toolchain clang version probe"
    "release evidence [evidc --check-toolchain] clang version probe"
    "non-empty value after"
    "clang version:"
    "empty-check-toolchain-clang-version-evident-release-evidence.txt"
)

write_release_evidence("${ambiguous_check_toolchain_clang_version_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${ambiguous_check_toolchain_clang_version_evidence_path}" ambiguous_check_toolchain_clang_version_evidence)
string(REPLACE
    "clang version: clang version synthetic\n"
    "clang version: version synthetic\n"
    ambiguous_check_toolchain_clang_version_evidence
    "${ambiguous_check_toolchain_clang_version_evidence}"
)
file(WRITE "${ambiguous_check_toolchain_clang_version_evidence_path}" "${ambiguous_check_toolchain_clang_version_evidence}")
assert_release_evidence_rejected(
    "${ambiguous_check_toolchain_clang_version_evidence_path}"
    "release evidence validation unexpectedly accepted checked-toolchain clang version output that does not identify clang"
    "release evidence [evidc --check-toolchain] clang version probe"
    "invalid"
    "version synthetic"
    "clang version:"
    "ambiguous-check-toolchain-clang-version-evident-release-evidence.txt"
)

write_release_evidence("${ambiguous_check_toolchain_lld_link_version_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${ambiguous_check_toolchain_lld_link_version_evidence_path}" ambiguous_check_toolchain_lld_link_version_evidence)
string(REPLACE
    "lld-link version: LLD synthetic\n"
    "lld-link version: version synthetic\n"
    ambiguous_check_toolchain_lld_link_version_evidence
    "${ambiguous_check_toolchain_lld_link_version_evidence}"
)
file(WRITE "${ambiguous_check_toolchain_lld_link_version_evidence_path}" "${ambiguous_check_toolchain_lld_link_version_evidence}")
assert_release_evidence_rejected(
    "${ambiguous_check_toolchain_lld_link_version_evidence_path}"
    "release evidence validation unexpectedly accepted checked-toolchain lld-link version output that does not identify LLD"
    "release evidence [evidc --check-toolchain] lld-link version probe"
    "invalid"
    "version synthetic"
    "lld-link version:"
    "ambiguous-check-toolchain-lld-link-version-evident-release-evidence.txt"
)

write_release_evidence("${mismatched_evidc_version_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${mismatched_evidc_version_evidence_path}" mismatched_evidc_version_evidence)
string(REPLACE
    "[evidc --version]\nevidc 0.1.0\n"
    "[evidc --version]\nevidc 0.2.0\n"
    mismatched_evidc_version_evidence
    "${mismatched_evidc_version_evidence}"
)
file(WRITE "${mismatched_evidc_version_evidence_path}" "${mismatched_evidc_version_evidence}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${mismatched_evidc_version_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE mismatched_evidc_version_result
    OUTPUT_VARIABLE mismatched_evidc_version_stdout
    ERROR_VARIABLE mismatched_evidc_version_stderr
)

if(mismatched_evidc_version_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted a compiler version that does not match the release package name\nstdout:\n${mismatched_evidc_version_stdout}\nstderr:\n${mismatched_evidc_version_stderr}")
endif()

set(actual_error "${mismatched_evidc_version_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence [evidc --version] section must exactly match release"
    "package version 'evidc 0.1.0'"
    "mismatched-evidc-version-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "mismatched-evidc-version release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${mismatched_evidc_version_stdout}\nstderr:\n${mismatched_evidc_version_stderr}")
    endif()
endforeach()

write_release_evidence("${mismatched_native_target_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${mismatched_native_target_evidence_path}" mismatched_native_target_evidence)
string(REPLACE
    "runner image version: synthetic.image.version\nnative target: x86_64-pc-windows-msvc\n"
    "runner image version: synthetic.image.version\nnative target: x86_64-unknown-linux-gnu\n"
    mismatched_native_target_evidence
    "${mismatched_native_target_evidence}"
)
file(WRITE "${mismatched_native_target_evidence_path}" "${mismatched_native_target_evidence}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${mismatched_native_target_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE mismatched_native_target_result
    OUTPUT_VARIABLE mismatched_native_target_stdout
    ERROR_VARIABLE mismatched_native_target_stderr
)

if(mismatched_native_target_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted a mismatched top-level native target\nstdout:\n${mismatched_native_target_stdout}\nstderr:\n${mismatched_native_target_stderr}")
endif()

set(actual_error "${mismatched_native_target_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence top provenance native target line must contain exactly"
    "native target: x86_64-pc-windows-msvc"
    "found 0"
    "mismatched-native-target-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "mismatched-native-target release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${mismatched_native_target_stdout}\nstderr:\n${mismatched_native_target_stderr}")
    endif()
endforeach()

write_release_evidence("${relative_tool_path_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${relative_tool_path_evidence_path}" relative_tool_path_evidence)
string(REGEX REPLACE
    "\\[where cmake\\]\n[^\n]+"
    "[where cmake]\ncmake.exe"
    relative_tool_path_evidence
    "${relative_tool_path_evidence}"
)
file(WRITE "${relative_tool_path_evidence_path}" "${relative_tool_path_evidence}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${relative_tool_path_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE relative_tool_path_result
    OUTPUT_VARIABLE relative_tool_path_stdout
    ERROR_VARIABLE relative_tool_path_stderr
)

if(relative_tool_path_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted a relative tool path\nstdout:\n${relative_tool_path_stdout}\nstderr:\n${relative_tool_path_stderr}")
endif()

set(actual_error "${relative_tool_path_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence is missing expected absolute cmake path from"
    "relative-tool-path-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "relative-tool-path release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${relative_tool_path_stdout}\nstderr:\n${relative_tool_path_stderr}")
    endif()
endforeach()

write_release_evidence("${mixed_tool_path_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${mixed_tool_path_evidence_path}" mixed_tool_path_evidence)
string(REPLACE
    "[where cmake]\nC:\\synthetic\\cmake.exe\n"
    "[where cmake]\nC:\\synthetic\\cmake.exe\ncmake.exe\n"
    mixed_tool_path_evidence
    "${mixed_tool_path_evidence}"
)
file(WRITE "${mixed_tool_path_evidence_path}" "${mixed_tool_path_evidence}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${mixed_tool_path_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE mixed_tool_path_result
    OUTPUT_VARIABLE mixed_tool_path_stdout
    ERROR_VARIABLE mixed_tool_path_stderr
)

if(mixed_tool_path_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted mixed absolute and relative tool paths\nstdout:\n${mixed_tool_path_stdout}\nstderr:\n${mixed_tool_path_stderr}")
endif()

set(actual_error "${mixed_tool_path_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence [where cmake] section must contain only absolute Windows"
    "paths; found 'cmake.exe'"
    "mixed-tool-path-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "mixed-tool-path release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${mixed_tool_path_stdout}\nstderr:\n${mixed_tool_path_stderr}")
    endif()
endforeach()

write_release_evidence("${invalid_commit_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${invalid_commit_evidence_path}" invalid_commit_evidence)
string(REPLACE
    "commit: ${commit_sha}"
    "commit: synthetic-test-commit"
    invalid_commit_evidence
    "${invalid_commit_evidence}"
)
file(WRITE "${invalid_commit_evidence_path}" "${invalid_commit_evidence}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${invalid_commit_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE invalid_commit_result
    OUTPUT_VARIABLE invalid_commit_stdout
    ERROR_VARIABLE invalid_commit_stderr
)

if(invalid_commit_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted an invalid commit SHA\nstdout:\n${invalid_commit_stdout}\nstderr:\n${invalid_commit_stderr}")
endif()

set(actual_error "${invalid_commit_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence commit must be a lowercase 40-character Git SHA"
    "synthetic-test-commit"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "invalid-commit release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${invalid_commit_stdout}\nstderr:\n${invalid_commit_stderr}")
    endif()
endforeach()

write_release_evidence("${mismatched_expected_commit_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${mismatched_expected_commit_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DEXPECTED_COMMIT_SHA=${other_commit_sha}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE mismatched_expected_commit_result
    OUTPUT_VARIABLE mismatched_expected_commit_stdout
    ERROR_VARIABLE mismatched_expected_commit_stderr
)

if(mismatched_expected_commit_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted a commit that did not match the expected source commit\nstdout:\n${mismatched_expected_commit_stdout}\nstderr:\n${mismatched_expected_commit_stderr}")
endif()

set(actual_error "${mismatched_expected_commit_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence commit must match expected source commit"
    "${other_commit_sha}"
    "${commit_sha}"
    "mismatched-expected-commit-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "mismatched-expected-commit release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${mismatched_expected_commit_stdout}\nstderr:\n${mismatched_expected_commit_stderr}")
    endif()
endforeach()

write_release_evidence("${duplicate_commit_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${duplicate_commit_evidence_path}" duplicate_commit_evidence)
string(REPLACE
    "commit: ${commit_sha}\n"
    "commit: ${commit_sha}\ncommit: ${commit_sha}\n"
    duplicate_commit_evidence
    "${duplicate_commit_evidence}"
)
file(WRITE "${duplicate_commit_evidence_path}" "${duplicate_commit_evidence}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${duplicate_commit_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE duplicate_commit_result
    OUTPUT_VARIABLE duplicate_commit_stdout
    ERROR_VARIABLE duplicate_commit_stderr
)

if(duplicate_commit_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted a duplicate commit line\nstdout:\n${duplicate_commit_stdout}\nstderr:\n${duplicate_commit_stderr}")
endif()

set(actual_error "${duplicate_commit_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence must contain exactly one commit line"
    "found 2"
    "duplicate-commit-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "duplicate-commit release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${duplicate_commit_stdout}\nstderr:\n${duplicate_commit_stderr}")
    endif()
endforeach()

write_release_evidence("${conflicting_zip_sha_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${conflicting_zip_sha_evidence_path}" conflicting_zip_sha_evidence)
string(REPLACE
    "sha256: ${package_sha256}\n"
    "sha256: ${package_sha256}\nsha256: ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\n"
    conflicting_zip_sha_evidence
    "${conflicting_zip_sha_evidence}"
)
file(WRITE "${conflicting_zip_sha_evidence_path}" "${conflicting_zip_sha_evidence}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${conflicting_zip_sha_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE conflicting_zip_sha_result
    OUTPUT_VARIABLE conflicting_zip_sha_stdout
    ERROR_VARIABLE conflicting_zip_sha_stderr
)

if(conflicting_zip_sha_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted a conflicting ZIP SHA256 line\nstdout:\n${conflicting_zip_sha_stdout}\nstderr:\n${conflicting_zip_sha_stderr}")
endif()

set(actual_error "${conflicting_zip_sha_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence must contain exactly one release ZIP SHA256 line"
    "found 2"
    "conflicting-zip-sha-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "conflicting-zip-sha release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${conflicting_zip_sha_stdout}\nstderr:\n${conflicting_zip_sha_stderr}")
    endif()
endforeach()

write_release_evidence("${duplicated_package_details_before_zip_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${duplicated_package_details_before_zip_evidence_path}" duplicated_package_details_before_zip_evidence)
string(REPLACE
    "[ninja --version]\n1.0.synthetic\n"
    "[ninja --version]\n1.0.synthetic\nfile: evident-9.9.9-windows-x64.zip\nbytes: 54321\nsha256: ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\n"
    duplicated_package_details_before_zip_evidence
    "${duplicated_package_details_before_zip_evidence}"
)
file(WRITE "${duplicated_package_details_before_zip_evidence_path}" "${duplicated_package_details_before_zip_evidence}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${duplicated_package_details_before_zip_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE duplicated_package_details_before_zip_result
    OUTPUT_VARIABLE duplicated_package_details_before_zip_stdout
    ERROR_VARIABLE duplicated_package_details_before_zip_stderr
)

if(duplicated_package_details_before_zip_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted package details duplicated before the release ZIP section\nstdout:\n${duplicated_package_details_before_zip_stdout}\nstderr:\n${duplicated_package_details_before_zip_stderr}")
endif()

set(actual_error "${duplicated_package_details_before_zip_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence before [release ZIP] package file lines must not contain"
    "lines starting with 'file:'"
    "duplicated-package-details-before-zip-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "duplicated-package-details-before-zip release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${duplicated_package_details_before_zip_stdout}\nstderr:\n${duplicated_package_details_before_zip_stderr}")
    endif()
endforeach()

write_release_evidence("${misplaced_package_details_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
file(READ "${misplaced_package_details_evidence_path}" misplaced_package_details_evidence)
string(REPLACE
    "[release ZIP]\nfile: ${package_name}\nbytes: ${package_size}\nsha256: ${package_sha256}"
    "[release ZIP]\nfile: evident-9.9.9-windows-x64.zip\nbytes: 54321\nsha256: ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
    misplaced_package_details_evidence
    "${misplaced_package_details_evidence}"
)
string(REPLACE
    "[ninja --version]\n1.0.synthetic\n"
    "[ninja --version]\n1.0.synthetic\nfile: ${package_name}\nbytes: ${package_size}\nsha256: ${package_sha256}\n"
    misplaced_package_details_evidence
    "${misplaced_package_details_evidence}"
)
file(WRITE "${misplaced_package_details_evidence_path}" "${misplaced_package_details_evidence}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${misplaced_package_details_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE misplaced_package_details_result
    OUTPUT_VARIABLE misplaced_package_details_stdout
    ERROR_VARIABLE misplaced_package_details_stderr
)

if(misplaced_package_details_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted misplaced package details\nstdout:\n${misplaced_package_details_stdout}\nstderr:\n${misplaced_package_details_stderr}")
endif()

set(actual_error "${misplaced_package_details_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release evidence [release ZIP] package file line must contain exactly"
    "found 0"
    "misplaced-package-details-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "misplaced-package-details release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${misplaced_package_details_stdout}\nstderr:\n${misplaced_package_details_stderr}")
    endif()
endforeach()

write_release_evidence("${invalid_package_name_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
set(invalid_package_name "evident-dev-windows-x64.zip")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${invalid_package_name_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${invalid_package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE invalid_package_name_result
    OUTPUT_VARIABLE invalid_package_name_stdout
    ERROR_VARIABLE invalid_package_name_stderr
)

if(invalid_package_name_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted an invalid package name\nstdout:\n${invalid_package_name_stdout}\nstderr:\n${invalid_package_name_stderr}")
endif()

set(actual_error "${invalid_package_name_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "RELEASE_PACKAGE_NAME must match evident-<version>-windows-x64.zip"
    "${invalid_package_name}"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "invalid-package-name release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${invalid_package_name_stdout}\nstderr:\n${invalid_package_name_stderr}")
    endif()
endforeach()

write_release_evidence("${invalid_package_size_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
set(invalid_package_size "0")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${invalid_package_size_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${invalid_package_size}"
        "-DRELEASE_PACKAGE_SHA256=${package_sha256}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE invalid_package_size_result
    OUTPUT_VARIABLE invalid_package_size_stdout
    ERROR_VARIABLE invalid_package_size_stderr
)

if(invalid_package_size_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted an invalid package size\nstdout:\n${invalid_package_size_stdout}\nstderr:\n${invalid_package_size_stderr}")
endif()

set(actual_error "${invalid_package_size_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "RELEASE_PACKAGE_SIZE must be a positive decimal byte count"
    "${invalid_package_size}"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "invalid-package-size release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${invalid_package_size_stdout}\nstderr:\n${invalid_package_size_stderr}")
    endif()
endforeach()

write_release_evidence("${invalid_package_sha_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
set(invalid_package_sha "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${invalid_package_sha_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-DRELEASE_PACKAGE_NAME=${package_name}"
        "-DRELEASE_PACKAGE_SIZE=${package_size}"
        "-DRELEASE_PACKAGE_SHA256=${invalid_package_sha}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE invalid_package_sha_result
    OUTPUT_VARIABLE invalid_package_sha_stdout
    ERROR_VARIABLE invalid_package_sha_stderr
)

if(invalid_package_sha_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted an invalid package SHA256\nstdout:\n${invalid_package_sha_stdout}\nstderr:\n${invalid_package_sha_stderr}")
endif()

set(actual_error "${invalid_package_sha_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "RELEASE_PACKAGE_SHA256 must be a lowercase 64-character SHA256 hex digest"
    "${invalid_package_sha}"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "invalid-package-sha release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${invalid_package_sha_stdout}\nstderr:\n${invalid_package_sha_stderr}")
    endif()
endforeach()

write_release_evidence("${missing_package_details_evidence_path}" TRUE "${EXPECTED_CTEST_TOTAL}")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DEVIDENCE_PATH=${missing_package_details_evidence_path}"
        "-DEXPECTED_CTEST_TOTAL=${EXPECTED_CTEST_TOTAL}"
        "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseEvidence.cmake"
    RESULT_VARIABLE missing_package_details_result
    OUTPUT_VARIABLE missing_package_details_stdout
    ERROR_VARIABLE missing_package_details_stderr
)

if(missing_package_details_result EQUAL 0)
    message(FATAL_ERROR
        "release evidence validation unexpectedly accepted missing release package details\nstdout:\n${missing_package_details_stdout}\nstderr:\n${missing_package_details_stderr}")
endif()

set(actual_error "${missing_package_details_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")
foreach(required_fragment IN ITEMS
    "release package evidence details must provide RELEASE_PACKAGE_NAME"
    "RELEASE_PACKAGE_SIZE"
    "RELEASE_PACKAGE_SHA256"
    "missing-package-details-evident-release-evidence.txt"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "missing-package-details release evidence error did not contain required fragment '${required_fragment}'\nstdout:\n${missing_package_details_stdout}\nstderr:\n${missing_package_details_stderr}")
    endif()
endforeach()
