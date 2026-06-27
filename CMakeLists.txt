cmake_minimum_required(VERSION 3.24)
project(evident_compiler VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(evident_frontend
    src/Source.cpp
    src/Diagnostic.cpp
    src/Token.cpp
    src/Lexer.cpp
    src/Ast.cpp
    src/Parser.cpp
    src/ResolvedType.cpp
    src/Semantic.cpp
    src/Hir.cpp
    src/Mir.cpp
    src/Backend.cpp
    src/Driver.cpp
)

target_include_directories(evident_frontend PUBLIC include)

target_compile_options(evident_frontend PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

add_executable(evidc src/main.cpp)
target_link_libraries(evidc PRIVATE evident_frontend)
target_compile_definitions(evidc PRIVATE EVIDENT_VERSION="${PROJECT_VERSION}")

install(TARGETS evidc
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES README.md LICENSE
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

install(DIRECTORY docs/
    DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs
    FILES_MATCHING PATTERN "*.md"
)

file(GLOB_RECURSE EVIDENT_RELEASE_DOC_FILES CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/docs/*.md"
)
set(EVIDENT_RELEASE_DOCS README.md LICENSE ${EVIDENT_RELEASE_DOC_FILES})

set(CPACK_PACKAGE_NAME "evident")
set(CPACK_PACKAGE_VENDOR "Evident")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Evident compiler subset release")
set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-windows-x64")
set(EVIDENT_PACKAGE_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/${CPACK_PACKAGE_FILE_NAME}.zip")
set(EVIDENT_PACKAGE_SHA256 "${EVIDENT_PACKAGE_ARCHIVE}.sha256")
set(EVIDENT_PACKAGE_ROOT "${CPACK_PACKAGE_FILE_NAME}")
set(EVIDENT_PACKAGE_EXECUTABLE_ENTRY "${EVIDENT_PACKAGE_ROOT}/${CMAKE_INSTALL_BINDIR}/$<TARGET_FILE_NAME:evidc>")
set(EVIDENT_EXPECTED_PACKAGE_ENTRIES "${EVIDENT_PACKAGE_EXECUTABLE_ENTRY}")
foreach(release_doc IN LISTS EVIDENT_RELEASE_DOCS)
    list(APPEND EVIDENT_EXPECTED_PACKAGE_ENTRIES "${EVIDENT_PACKAGE_ROOT}/${CMAKE_INSTALL_DOCDIR}/${release_doc}")
endforeach()
include(CPack)

add_custom_target(package_checksum
    COMMAND ${CMAKE_COMMAND}
        -DPACKAGE_PATH=${EVIDENT_PACKAGE_ARCHIVE}
        -DOUTPUT_PATH=${EVIDENT_PACKAGE_SHA256}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/WritePackageSha256.cmake
    BYPRODUCTS ${EVIDENT_PACKAGE_SHA256}
    COMMENT "Writing package SHA256 checksum"
    VERBATIM
)
add_dependencies(package_checksum package)

include(CTest)
if(BUILD_TESTING)
    set(EVIDENT_EXPECTED_CTEST_TOTAL 377)

    add_test(NAME parse_valid_example
        COMMAND evidc --dump-hir ${CMAKE_CURRENT_SOURCE_DIR}/examples/feature_mode.evd)

    add_test(NAME dump_valid_example_mir
        COMMAND evidc --dump-mir ${CMAKE_CURRENT_SOURCE_DIR}/examples/feature_mode.evd)

    add_test(NAME parse_valid_yield_match
        COMMAND evidc ${CMAKE_CURRENT_SOURCE_DIR}/tests/valid_yield_match.evd)

    add_test(NAME validate_install_layout
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DEXECUTABLE_NAME=$<TARGET_FILE_NAME:evidc>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DINSTALL_BINDIR=${CMAKE_INSTALL_BINDIR}
            -DINSTALL_DOCDIR=${CMAKE_INSTALL_DOCDIR}
            -DSMOKE_INPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/examples/feature_mode.evd
            -DEXPECTED_VERSION_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected_cli_version.stdout.out
            -DEXPECTED_HELP_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected_cli_help.stdout.out
            -DEXPECTED_TOOLCHAIN_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected_cli_print_toolchain.stdout.out
            "-DEXPECTED_DOCS=${EVIDENT_RELEASE_DOCS}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunInstallAndAssertLayout.cmake)

    add_test(NAME validate_install_layout_rejects_unexpected_entry
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DEXECUTABLE_NAME=$<TARGET_FILE_NAME:evidc>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DINSTALL_BINDIR=${CMAKE_INSTALL_BINDIR}
            -DINSTALL_DOCDIR=${CMAKE_INSTALL_DOCDIR}
            -DSMOKE_INPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/examples/feature_mode.evd
            -DEXPECTED_VERSION_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected_cli_version.stdout.out
            -DEXPECTED_HELP_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected_cli_help.stdout.out
            -DEXPECTED_TOOLCHAIN_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected_cli_print_toolchain.stdout.out
            "-DEXPECTED_DOCS=${EVIDENT_RELEASE_DOCS}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunInstallRejectsUnexpectedEntry.cmake)

    add_test(NAME validate_package_zip
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DPACKAGE_PATH=${EVIDENT_PACKAGE_ARCHIVE}
            -DEXECUTABLE_ENTRY=${EVIDENT_PACKAGE_EXECUTABLE_ENTRY}
            -DINSTALL_DOCDIR=${CMAKE_INSTALL_DOCDIR}
            -DSMOKE_INPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/examples/feature_mode.evd
            -DEXPECTED_VERSION_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected_cli_version.stdout.out
            -DEXPECTED_HELP_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected_cli_help.stdout.out
            -DEXPECTED_TOOLCHAIN_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected_cli_print_toolchain.stdout.out
            "-DEXPECTED_ENTRIES=${EVIDENT_EXPECTED_PACKAGE_ENTRIES}"
            "-DEXPECTED_DOCS=${EVIDENT_RELEASE_DOCS}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunPackageAndAssertZip.cmake)

    add_test(NAME validate_package_checksum
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DPACKAGE_PATH=${EVIDENT_PACKAGE_ARCHIVE}
            -DCHECKSUM_PATH=${EVIDENT_PACKAGE_SHA256}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunPackageChecksumAndAssert.cmake)

    add_test(NAME validate_package_checksum_rejects_tampered_package
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunPackageChecksumRejectsTamperedPackage.cmake)

    add_test(NAME validate_release_evidence_contract
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DEXPECTED_CTEST_TOTAL=${EVIDENT_EXPECTED_CTEST_TOTAL}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunReleaseEvidenceAndAssert.cmake)

    add_test(NAME validate_ctest_total_contract
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}
            -DEXPECTED_CTEST_TOTAL=${EVIDENT_EXPECTED_CTEST_TOTAL}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertCTestTotal.cmake)

    add_test(NAME validate_ctest_total_rejects_stale_count
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DCTEST_COMMAND=${CMAKE_CTEST_COMMAND}
            -DEXPECTED_CTEST_TOTAL=${EVIDENT_EXPECTED_CTEST_TOTAL}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunCTestTotalRejectsStaleCount.cmake)

    add_test(NAME validate_release_docs_contract
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunReleaseDocsAndAssert.cmake)

    add_test(NAME validate_release_source_tree_contract
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunReleaseSourceTreeAndAssert.cmake)

    add_test(NAME validate_ci_workflow_contract
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWORKFLOW_PATH=${CMAKE_CURRENT_SOURCE_DIR}/.github/workflows/ci.yml
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunCiWorkflowAndAssert.cmake)

    add_test(NAME validate_cpp_design_escape_hatches
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunCppDesignEscapeHatchesAndAssert.cmake)

    add_test(NAME validate_invalid_corpus_registered
        COMMAND ${CMAKE_COMMAND}
            -DTESTS_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests
            -DCMAKE_LIST_PATH=${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertInvalidCorpusRegistered.cmake)

    add_test(NAME validate_test_corpus_registered
        COMMAND ${CMAKE_COMMAND}
            -DTESTS_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests
            -DCMAKE_LIST_PATH=${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/AssertTestCorpusRegistered.cmake)

    add_test(NAME validate_test_corpus_registration_rejects_unregistered_artifact
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunTestCorpusRegistrationRejectsUnregistered.cmake)

    add_test(NAME validate_package_zip_rejects_unexpected_file
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunPackageZipRejectsUnexpectedFile.cmake)

    add_test(NAME validate_package_zip_rejects_unexpected_directory
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunPackageZipRejectsUnexpectedDirectory.cmake)

    add_test(NAME validate_package_zip_rejects_missing_expected_entry
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunPackageZipRejectsMissingExpectedEntry.cmake)

    add_test(NAME validate_package_zip_rejects_unsafe_entries
        COMMAND ${CMAKE_COMMAND}
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunPackageZipRejectsUnsafeEntries.cmake)

    set_tests_properties(validate_install_layout validate_package_zip validate_package_checksum
        PROPERTIES
            RUN_SERIAL TRUE
            ENVIRONMENT "EVIDENT_CLANG=clang")

    function(add_mir_compare_test name input_file expected_file)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DMODE=--dump-mir
                -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}
                -DEXPECTED_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${expected_file}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunAndCompareOutput.cmake)
    endfunction()

    function(add_multi_mir_compare_test name expected_file)
        set(input_paths)
        foreach(input_file IN LISTS ARGN)
            list(APPEND input_paths "${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}")
        endforeach()
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DMODE=--dump-mir
                "-DINPUT_PATHS=${input_paths}"
                -DEXPECTED_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${expected_file}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunAndCompareOutput.cmake)
    endfunction()

    function(add_hir_compare_test name input_file expected_file)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DMODE=--dump-hir
                -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}
                -DEXPECTED_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${expected_file}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunAndCompareOutput.cmake)
    endfunction()

    function(add_multi_hir_compare_test name expected_file)
        set(input_paths)
        foreach(input_file IN LISTS ARGN)
            list(APPEND input_paths "${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}")
        endforeach()
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DMODE=--dump-hir
                "-DINPUT_PATHS=${input_paths}"
                -DEXPECTED_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${expected_file}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunAndCompareOutput.cmake)
    endfunction()

    function(add_multi_ast_compare_test name expected_file)
        set(input_paths)
        foreach(input_file IN LISTS ARGN)
            list(APPEND input_paths "${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}")
        endforeach()
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DMODE=--dump-ast
                "-DINPUT_PATHS=${input_paths}"
                -DEXPECTED_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${expected_file}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunAndCompareOutput.cmake)
    endfunction()

    function(add_package_hir_compare_test name package_dir expected_file)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DMODE=--dump-hir
                -DPACKAGE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/${package_dir}
                -DEXPECTED_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${expected_file}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunAndCompareOutput.cmake)
    endfunction()

    function(add_emit_compare_test name emit_mode input_file expected_file output_name)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DEMIT_MODE=${emit_mode}
                -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}
                -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/${output_name}
                -DEXPECTED_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${expected_file}
                -DTARGET_TRIPLE=x86_64-pc-windows-msvc
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunEmitAndCompareFile.cmake)
    endfunction()

    function(add_emit_exists_test name emit_mode input_file output_name)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DEMIT_MODE=${emit_mode}
                -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}
                -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/${output_name}
                -DTARGET_TRIPLE=x86_64-pc-windows-msvc
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunEmitAndAssertExists.cmake)
    endfunction()

    function(add_emit_coff_object_test name input_file output_name)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}
                -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/${output_name}
                -DTARGET_TRIPLE=x86_64-pc-windows-msvc
                -DEXPECTED_MACHINE_HEX=6486
                -DEXPECTED_OPTIONAL_HEADER_SIZE_HEX=0000
                -DEXPECTED_CHARACTERISTICS_HEX=0000
                -DEXPECTED_FIRST_SECTION_NAME_HEX=2e74657874000000
                -DEXPECTED_FIRST_SECTION_CHARACTERISTICS_HEX=20005060
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunEmitAndAssertCoffObject.cmake)
    endfunction()

    function(add_emit_pe_executable_test name input_file output_name)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}
                -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/${output_name}
                -DTARGET_TRIPLE=x86_64-pc-windows-msvc
                -DEXPECTED_MACHINE_HEX=6486
                -DEXPECTED_OPTIONAL_HEADER_SIZE_HEX=f000
                -DEXPECTED_CHARACTERISTICS_HEX=2200
                -DEXPECTED_OPTIONAL_MAGIC_HEX=0b02
                -DEXPECTED_SUBSYSTEM_HEX=0300
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunEmitAndAssertPeExecutable.cmake)
    endfunction()

    function(add_emit_pattern_test name input_file output_name contains_pattern not_contains_pattern)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DEMIT_MODE=--emit-llvm
                -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}
                -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/${output_name}
                "-DCONTAINS=${contains_pattern}"
                "-DNOT_CONTAINS=${not_contains_pattern}"
                -DTARGET_TRIPLE=x86_64-pc-windows-msvc
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunEmitAndAssertPattern.cmake)
    endfunction()

    function(add_emit_asm_pattern_test name input_file output_name contains_pattern not_contains_pattern)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DEMIT_MODE=--emit-asm
                -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}
                -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/${output_name}
                "-DCONTAINS=${contains_pattern}"
                "-DNOT_CONTAINS=${not_contains_pattern}"
                -DTARGET_TRIPLE=x86_64-pc-windows-msvc
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunEmitAndAssertPattern.cmake)
    endfunction()

    function(add_emit_asm_shape_test name input_file output_name)
        set(contains_patterns ${ARGN})
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DEMIT_MODE=--emit-asm
                -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}
                -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/${output_name}
                "-DCONTAINS_PATTERNS=${contains_patterns}"
                -DNOT_CONTAINS=mainCRTStartup
                -DTARGET_TRIPLE=x86_64-pc-windows-msvc
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunEmitAndAssertPattern.cmake)
    endfunction()

    function(add_emit_exe_test name input_file expected_exit output_name)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}
                -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/${output_name}
                -DEXPECTED_EXIT=${expected_exit}
                -DTARGET_TRIPLE=x86_64-pc-windows-msvc
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunEmitAndCheckExit.cmake)
    endfunction()

    function(add_multi_emit_exe_test name expected_exit output_name)
        set(input_paths)
        foreach(input_file IN LISTS ARGN)
            list(APPEND input_paths "${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}")
        endforeach()
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                "-DINPUT_PATHS=${input_paths}"
                -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/${output_name}
                -DEXPECTED_EXIT=${expected_exit}
                -DTARGET_TRIPLE=x86_64-pc-windows-msvc
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunEmitAndCheckExit.cmake)
    endfunction()

    function(add_package_emit_exe_test name package_dir expected_exit output_name)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DPACKAGE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/tests/${package_dir}
                -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/${output_name}
                -DEXPECTED_EXIT=${expected_exit}
                -DTARGET_TRIPLE=x86_64-pc-windows-msvc
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunEmitAndCheckExit.cmake)
    endfunction()

    function(add_cli_failure_compare_test name expected_file)
        set(command_args ${ARGN})
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                "-DCOMMAND_ARGS=${command_args}"
                -DEXPECTED_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${expected_file}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunCliFailureAndCompare.cmake)
    endfunction()

    function(add_cli_failure_compare_test_with_normalized_path name expected_file normalized_path)
        set(command_args ${ARGN})
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                "-DCOMMAND_ARGS=${command_args}"
                -DEXPECTED_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${expected_file}
                "-DNORMALIZE_PATHS=${normalized_path}"
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunCliFailureAndCompare.cmake)
    endfunction()

    function(add_cli_success_compare_test name expected_file)
        set(command_args ${ARGN})
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                "-DCOMMAND_ARGS=${command_args}"
                -DEXPECTED_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${expected_file}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunCliSuccessAndCompare.cmake)
    endfunction()

    function(add_cli_check_toolchain_test name expected_prefix_file)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DEXPECTED_PREFIX_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${expected_prefix_file}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunCliCheckToolchain.cmake)
    endfunction()

    function(add_cli_check_toolchain_missing_linker_test name clang_path)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DCLANG_PATH=${clang_path}
                -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunCliCheckToolchainMissingLinker.cmake)
    endfunction()

    find_program(EVIDENT_TEST_CLANG NAMES clang)
    if(NOT EVIDENT_TEST_CLANG)
        message(STATUS "clang was not found at configure time; cli_check_toolchain_missing_linker will not be added")
    endif()

    function(add_reject_test name file)
        add_test(NAME ${name}
            COMMAND evidc ${CMAKE_CURRENT_SOURCE_DIR}/tests/${file})
        set_tests_properties(${name} PROPERTIES WILL_FAIL TRUE)
    endfunction()

    function(add_backend_reject_test name file output_name)
        add_test(NAME ${name}
            COMMAND evidc --target x86_64-pc-windows-msvc --emit-exe ${CMAKE_CURRENT_BINARY_DIR}/${output_name}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/${file})
        set_tests_properties(${name} PROPERTIES WILL_FAIL TRUE)
    endfunction()

    function(add_diagnostic_compare_test name input_file expected_file)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}
                -DEXPECTED_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${expected_file}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunFailureAndCompareDiagnostics.cmake)
    endfunction()

    function(add_multi_diagnostic_compare_test name expected_file)
        set(input_paths)
        foreach(input_file IN LISTS ARGN)
            list(APPEND input_paths "${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}")
        endforeach()
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                "-DINPUT_PATHS=${input_paths}"
                -DEXPECTED_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${expected_file}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunFailureAndCompareDiagnostics.cmake)
    endfunction()

    function(add_backend_diagnostic_compare_test name input_file expected_file output_name)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}
                -DEXPECTED_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${expected_file}
                -DEMIT_MODE=--emit-exe
                -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/${output_name}
                -DTARGET_TRIPLE=x86_64-pc-windows-msvc
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunFailureAndCompareDiagnostics.cmake)
    endfunction()

    function(add_backend_target_diagnostic_compare_test name input_file expected_file emit_mode output_name target_triple)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}
                -DEXPECTED_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${expected_file}
                -DEMIT_MODE=${emit_mode}
                -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/${output_name}
                -DTARGET_TRIPLE=${target_triple}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunFailureAndCompareDiagnostics.cmake)
    endfunction()

    function(add_toolchain_diagnostic_compare_test name input_file expected_file emit_mode output_name clang_driver)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}
                -DEXPECTED_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${expected_file}
                -DEMIT_MODE=${emit_mode}
                -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/${output_name}
                -DTARGET_TRIPLE=x86_64-pc-windows-msvc
                -DTOOLCHAIN_CLANG=${clang_driver}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunFailureAndCompareDiagnostics.cmake)
    endfunction()

    add_mir_compare_test(compare_mir_plain_call valid_plain_call.evd expected_valid_plain_call.mir.txt)
    add_mir_compare_test(compare_mir_state_match valid_state_match.evd expected_valid_state_match.mir.txt)
    add_mir_compare_test(compare_mir_try valid_try.evd expected_valid_try.mir.txt)
    add_mir_compare_test(compare_mir_direct_fail valid_direct_fail.evd expected_valid_direct_fail.mir.txt)
    add_mir_compare_test(compare_mir_yield_match valid_yield_match.evd expected_valid_yield_match.mir.txt)
    add_mir_compare_test(compare_mir_permit_proof valid_permit_proof.evd expected_valid_permit_proof.mir.out)
    add_mir_compare_test(compare_mir_affine_proof_move valid_affine_proof_move.evd expected_valid_affine_proof_move.mir.out)
    add_mir_compare_test(compare_mir_grant_fails_try valid_grant_fails_try.evd expected_valid_grant_fails_try.mir.out)
    add_mir_compare_test(compare_mir_grant_fails_match valid_grant_fails_match.evd expected_valid_grant_fails_match.mir.out)
    add_mir_compare_test(compare_mir_generic_function_call
        valid_generic_function_call.evd
        expected_valid_generic_function_call.mir.out)
    add_mir_compare_test(compare_mir_generic_record_construct
        valid_generic_record_construct.evd
        expected_valid_generic_record_construct.mir.out)
    add_mir_compare_test(compare_mir_nested_generic_record_construct
        valid_nested_generic_record_construct.evd
        expected_valid_nested_generic_record_construct.mir.out)
    add_mir_compare_test(compare_mir_multiple_generic_instantiations
        valid_multiple_generic_instantiations.evd
        expected_valid_multiple_generic_instantiations.mir.out)
    add_mir_compare_test(compare_mir_builtin_list_values
        valid_builtin_list_values.evd
        expected_valid_builtin_list_values.mir.out)
    add_hir_compare_test(compare_hir_affine_wrapper valid_affine_wrapper.evd expected_valid_affine_wrapper.hir.out)
    add_hir_compare_test(compare_hir_affine_generic_wrapper valid_affine_generic_wrapper.evd expected_valid_affine_generic_wrapper.hir.out)
    add_hir_compare_test(compare_hir_generic_function_call
        valid_generic_function_call.evd
        expected_valid_generic_function_call.hir.out)
    add_hir_compare_test(compare_hir_generic_record_construct
        valid_generic_record_construct.evd
        expected_valid_generic_record_construct.hir.out)
    add_hir_compare_test(compare_hir_nested_generic_record_construct
        valid_nested_generic_record_construct.evd
        expected_valid_nested_generic_record_construct.hir.out)
    add_hir_compare_test(compare_hir_multiple_generic_instantiations
        valid_multiple_generic_instantiations.evd
        expected_valid_multiple_generic_instantiations.hir.out)
    add_hir_compare_test(compare_hir_builtin_list_values
        valid_builtin_list_values.evd
        expected_valid_builtin_list_values.hir.out)
    add_hir_compare_test(compare_hir_collection_companion_records
        valid_collection_companion_records.evd
        expected_valid_collection_companion_records.hir.out)
    add_multi_hir_compare_test(compare_hir_package_cross_file
        expected_package_cross_file.hir.out
        package_support.evd
        package_consumer.evd)
    add_multi_hir_compare_test(compare_hir_package_split_module
        expected_package_split_module.hir.out
        package_split_first.evd
        package_split_second.evd)
    add_multi_ast_compare_test(compare_ast_package_import
        expected_package_import.ast.out
        package_import_support.evd
        package_import_consumer.evd)
    add_multi_hir_compare_test(compare_hir_package_import_gated
        expected_package_import_gated.hir.out
        package_import_value_support.evd
        package_import_gated_consumer.evd)
    add_multi_hir_compare_test(compare_hir_package_generic_import
        expected_package_generic_import.hir.out
        package_generic_support.evd
        package_generic_app.evd)
    add_multi_mir_compare_test(compare_mir_package_generic_import
        expected_package_generic_import.mir.out
        package_generic_support.evd
        package_generic_app.evd)
    add_multi_hir_compare_test(compare_hir_package_import_file_local_shared
        expected_package_import_file_local_shared.hir.out
        package_import_value_support.evd
        package_import_gated_consumer.evd
        package_import_gated_consumer_two.evd)
    add_package_hir_compare_test(compare_hir_package_directory_discovery
        package_dir
        expected_package_directory_discovery.hir.out)
    add_package_hir_compare_test(compare_hir_package_manifest
        package_manifest
        expected_package_manifest.hir.out)
    add_test(NAME parse_valid_copyable_generic_wrapper
        COMMAND evidc ${CMAKE_CURRENT_SOURCE_DIR}/tests/valid_copyable_generic_wrapper.evd)
    add_test(NAME parse_valid_affine_branch_diverge
        COMMAND evidc ${CMAKE_CURRENT_SOURCE_DIR}/tests/valid_affine_branch_diverge.evd)
    add_test(NAME parse_valid_grant_fails_try
        COMMAND evidc ${CMAKE_CURRENT_SOURCE_DIR}/tests/valid_grant_fails_try.evd)
    add_test(NAME parse_valid_grant_fails_match
        COMMAND evidc ${CMAKE_CURRENT_SOURCE_DIR}/tests/valid_grant_fails_match.evd)
    add_test(NAME parse_valid_phase_transition
        COMMAND evidc ${CMAKE_CURRENT_SOURCE_DIR}/tests/valid_phase_transition.evd)
    add_test(NAME parse_valid_multiple_proves_clauses
        COMMAND evidc ${CMAKE_CURRENT_SOURCE_DIR}/tests/valid_multiple_proves_clauses.evd)

    add_emit_compare_test(compare_llvm_plain_call --emit-llvm valid_plain_call.evd expected_valid_plain_call.llvm.txt compare_valid_plain_call.ll)
    add_emit_compare_test(compare_llvm_state_match --emit-llvm valid_state_match.evd expected_valid_state_match.llvm.txt compare_valid_state_match.ll)
    add_emit_compare_test(compare_llvm_try --emit-llvm valid_try.evd expected_valid_try.llvm.txt compare_valid_try.ll)
    add_emit_compare_test(compare_llvm_direct_fail --emit-llvm valid_direct_fail.evd expected_valid_direct_fail.llvm.txt compare_valid_direct_fail.ll)
    add_emit_compare_test(compare_llvm_yield_match --emit-llvm valid_yield_match.evd expected_valid_yield_match.llvm.txt compare_valid_yield_match.ll)
    add_emit_compare_test(compare_llvm_empty_collections --emit-llvm native_main_empty_collections.evd expected_native_main_empty_collections.llvm.txt compare_native_main_empty_collections.ll)

    add_emit_coff_object_test(validate_obj_plain_call_coff_x64 valid_plain_call.evd valid_plain_call.obj)
    add_emit_coff_object_test(validate_obj_state_match_coff_x64 valid_state_match.evd valid_state_match.obj)
    add_emit_coff_object_test(validate_obj_try_coff_x64 valid_try.evd valid_try.obj)
    add_emit_coff_object_test(validate_obj_direct_fail_coff_x64 valid_direct_fail.evd valid_direct_fail.obj)
    add_emit_coff_object_test(validate_obj_yield_match_coff_x64 valid_yield_match.evd valid_yield_match.obj)
    add_emit_exists_test(emit_obj_permit_proof --emit-obj valid_permit_proof.evd valid_permit_proof.obj)
    add_emit_exists_test(emit_asm_constant --emit-asm native_main_constant.evd native_main_constant.s)
    add_emit_exists_test(emit_obj_constant --emit-obj native_main_constant.evd native_main_constant.obj)
    add_emit_asm_pattern_test(validate_asm_constant_main_symbol
        native_main_constant.evd
        validate_native_main_constant.s
        "evid$test$main:"
        "")
    add_emit_asm_shape_test(validate_asm_constant_windows_x64_shape
        native_main_constant.evd
        validate_native_main_constant_shape.s
        ".text"
        "evid$test$main:"
        ".seh_proc evid$test$main"
        ".seh_endproc"
        "retq"
        ".addrsig")
    add_emit_coff_object_test(validate_obj_constant_coff_x64 native_main_constant.evd validate_native_main_constant.obj)
    add_emit_coff_object_test(validate_obj_permit_proof_coff_x64 valid_permit_proof.evd validate_valid_permit_proof.obj)

    add_emit_pattern_test(hygiene_llvm_plain_call_insertvalue
        valid_plain_call.evd
        hygiene_valid_plain_call.ll
        "insertvalue %evid.type.test.Value zeroinitializer, i64 %load0, 0"
        "getelementptr inbounds %evid.type.test.Value")
    add_emit_pattern_test(hygiene_llvm_try_invoke_extractvalue
        valid_try.evd
        hygiene_valid_try.ll
        "extractvalue %evid.yield.test.open %invoke1, 0"
        "invoke.slot")
    add_emit_pattern_test(hygiene_llvm_direct_fail_text_value
        valid_direct_fail.evd
        hygiene_valid_direct_fail.ll
        "insertvalue %evid.yield.test.boot zeroinitializer, i8 1, 0"
        "alloca { ptr, i64 }")
    add_emit_pattern_test(hygiene_llvm_yield_match_text_value
        valid_yield_match.evd
        hygiene_valid_yield_match.ll
        "insertvalue { ptr, i64 } zeroinitializer, ptr %strptr"
        "%lit")
    add_emit_pattern_test(hygiene_llvm_builtin_list_abi_shape
        valid_builtin_list_values.evd
        hygiene_valid_builtin_list_values.ll
        "define { ptr, i64 } @evid$test$keep_many({ ptr, i64 } %arg0)"
        "")
    add_emit_pattern_test(hygiene_llvm_permit_signature_erased
        valid_permit_proof.evd
        hygiene_valid_permit_proof_signature.ll
        "define i64 @evid$test$submit(%evid.type.test.Mesh %arg0)"
        "@evid$test$submit(%evid.type.RenderPassActive")
    add_emit_pattern_test(hygiene_llvm_permit_storage_erased
        valid_permit_proof.evd
        hygiene_valid_permit_proof_storage.ll
        "call i8 @evid$test$start_pass()"
        "%slot3 = alloca %evid.type.RenderPassActive")

    add_emit_exe_test(run_native_constant native_main_constant.evd 7 native_main_constant.exe)
    add_emit_exe_test(run_native_call_chain native_main_call_chain.evd 8 native_main_call_chain.exe)
    add_emit_exe_test(run_native_state_match native_main_state_match.evd 4 native_main_state_match.exe)
    add_emit_exe_test(run_native_try native_main_try.evd 11 native_main_try.exe)
    add_emit_exe_test(run_native_yield_match native_main_yield_match.evd 3 native_main_yield_match.exe)
    add_emit_exe_test(run_native_failing_match_discard_success
        native_main_failing_match_discard_success.evd
        5
        native_failing_match_discard_success.exe)
    add_emit_exe_test(run_native_with_permit native_main_with_permit.evd 7 native_main_with_permit.exe)
    add_emit_exe_test(run_native_phase_construct native_main_phase_construct.evd 7 native_main_phase_construct.exe)
    add_emit_exe_test(run_native_phase_copy_fields native_main_phase_copy_fields.evd 2 native_phase_copy_fields.exe)
    add_emit_exe_test(run_native_proof_field_access native_main_proof_field_access.evd 7 native_proof_field_access.exe)
    add_emit_exe_test(run_native_generic_affine_identity native_main_generic_affine_identity.evd 7 native_generic_affine_identity.exe)
    add_emit_exe_test(run_native_generic_affine_let_relay native_main_generic_affine_let_relay.evd 7 native_generic_affine_let_relay.exe)
    add_emit_exe_test(run_native_affine_state_payload_match native_main_affine_state_payload_match.evd 7 native_affine_state_payload_match.exe)
    add_emit_exe_test(run_native_generic_copy_duplicate native_main_generic_copy_duplicate.evd 7 native_generic_copy_duplicate.exe)
    add_emit_exe_test(run_native_generic_function_call valid_generic_function_call.evd 7 native_generic_function_call.exe)
    add_emit_exe_test(run_native_generic_record_construct valid_generic_record_construct.evd 7 native_generic_record_construct.exe)
    add_emit_exe_test(run_native_nested_generic_record_construct valid_nested_generic_record_construct.evd 7 native_nested_generic_record_construct.exe)
    add_emit_exe_test(run_native_multiple_generic_instantiations valid_multiple_generic_instantiations.evd 7 native_multiple_generic_instantiations.exe)
    add_emit_exe_test(run_native_builtin_list_values valid_builtin_list_values.evd 7 native_builtin_list_values.exe)
    add_multi_emit_exe_test(run_native_package_generic_import
        7
        native_package_generic_import.exe
        package_generic_support.evd
        package_generic_app.evd)
    add_multi_emit_exe_test(run_native_package_main
        13
        native_package_main.exe
        package_support.evd
        package_consumer.evd
        package_main.evd)
    add_package_emit_exe_test(run_native_package_directory_discovery
        package_dir
        17
        native_package_directory_discovery.exe)
    add_package_emit_exe_test(run_native_package_manifest
        package_manifest
        19
        native_package_manifest.exe)

    add_cli_success_compare_test(cli_help
        expected_cli_help.stdout.out
        --help)
    add_cli_success_compare_test(cli_version
        expected_cli_version.stdout.out
        --version)
    add_cli_success_compare_test(cli_print_toolchain
        expected_cli_print_toolchain.stdout.out
        --print-toolchain)
    set_tests_properties(cli_print_toolchain PROPERTIES ENVIRONMENT "EVIDENT_CLANG=clang")
    add_cli_check_toolchain_test(cli_check_toolchain
        expected_cli_print_toolchain.stdout.out)
    set_tests_properties(cli_check_toolchain PROPERTIES ENVIRONMENT "EVIDENT_CLANG=clang")
    add_cli_failure_compare_test(cli_check_toolchain_unsupported_target
        expected_cli_check_toolchain_unsupported_target.stderr.out
        --check-toolchain
        --target
        x86_64-unknown-linux-gnu)
    set(EVIDENT_WHITESPACE_CLANG_OVERRIDE "   ")
    add_cli_failure_compare_test(cli_print_toolchain_whitespace_clang_override
        expected_cli_print_toolchain_whitespace_clang_override.stderr.out
        --print-toolchain)
    set_tests_properties(cli_print_toolchain_whitespace_clang_override
        PROPERTIES ENVIRONMENT "EVIDENT_CLANG=${EVIDENT_WHITESPACE_CLANG_OVERRIDE}")
    add_cli_failure_compare_test(cli_check_toolchain_whitespace_clang_override
        expected_cli_check_toolchain_whitespace_clang_override.stderr.out
        --check-toolchain)
    set_tests_properties(cli_check_toolchain_whitespace_clang_override
        PROPERTIES ENVIRONMENT "EVIDENT_CLANG=${EVIDENT_WHITESPACE_CLANG_OVERRIDE}")
    if(EVIDENT_TEST_CLANG)
        add_cli_check_toolchain_missing_linker_test(cli_check_toolchain_missing_linker
            "${EVIDENT_TEST_CLANG}")
    endif()
    add_cli_failure_compare_test(cli_print_toolchain_with_input
        expected_cli_print_toolchain_with_input.stderr.out
        --print-toolchain
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_main_constant.evd)
    add_cli_failure_compare_test(cli_check_toolchain_with_input
        expected_cli_check_toolchain_with_input.stderr.out
        --check-toolchain
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_main_constant.evd)
    add_cli_failure_compare_test(cli_no_input
        expected_cli_no_input.stderr.out)
    add_cli_failure_compare_test(cli_unknown_option
        expected_cli_unknown_option.stderr.out
        --definitely-unknown)
    add_cli_failure_compare_test(cli_target_missing_argument
        expected_cli_target_missing_argument.stderr.out
        --target)
    add_cli_failure_compare_test(cli_duplicate_target
        expected_cli_duplicate_target.stderr.out
        --target
        x86_64-pc-windows-msvc
        --target
        x86_64-pc-windows-msvc
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_main_constant.evd)
    add_cli_failure_compare_test(cli_emit_exe_missing_argument
        expected_cli_emit_exe_missing_argument.stderr.out
        --emit-exe)
    add_cli_failure_compare_test(cli_multiple_native_emit_modes
        expected_cli_multiple_native_emit_modes.stderr.out
        --emit-llvm
        multiple.ll
        --emit-obj
        multiple.obj
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_main_constant.evd)
    add_cli_failure_compare_test(cli_duplicate_native_emit_mode
        expected_cli_multiple_native_emit_modes.stderr.out
        --emit-llvm
        first.ll
        --emit-llvm
        second.ll
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_main_constant.evd)
    add_cli_failure_compare_test(cli_duplicate_emit_stub
        expected_cli_duplicate_emit_stub.stderr.out
        --emit-stub
        first.stub
        --emit-stub
        second.stub
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_main_constant.evd)
    add_cli_failure_compare_test(cli_emit_stub_native_conflict
        expected_cli_emit_stub_native_conflict.stderr.out
        --emit-stub
        conflict.stub
        --emit-exe
        conflict.exe
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_main_constant.evd)
    add_cli_failure_compare_test_with_normalized_path(cli_missing_input_file
        expected_cli_missing_input_file.stderr.out
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/missing_input_file.evd
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/missing_input_file.evd)
    add_cli_failure_compare_test(cli_package_missing_argument
        expected_cli_package_missing_argument.stderr.out
        --package)
    add_cli_failure_compare_test(cli_duplicate_package
        expected_cli_duplicate_package.stderr.out
        --package
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_dir
        --package
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_manifest)
    add_cli_failure_compare_test(cli_package_cannot_mix_inputs
        expected_cli_package_cannot_mix_inputs.stderr.out
        --package
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_dir
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_support.evd)
    add_cli_failure_compare_test_with_normalized_path(cli_package_missing_directory
        expected_cli_package_missing_directory.stderr.out
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_missing
        --package
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_missing)
    add_cli_failure_compare_test_with_normalized_path(cli_package_path_is_file
        expected_cli_package_path_is_file.stderr.out
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_support.evd
        --package
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_support.evd)
    add_cli_failure_compare_test_with_normalized_path(cli_package_empty_directory
        expected_cli_package_empty_directory.stderr.out
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_empty
        --package
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_empty)
    add_cli_failure_compare_test_with_normalized_path(cli_package_manifest_empty
        expected_cli_package_manifest_empty.stderr.out
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_manifest_empty/evident.pkg
        --package
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_manifest_empty)
    add_cli_failure_compare_test_with_normalized_path(cli_package_manifest_bad_path
        expected_cli_package_manifest_bad_path.stderr.out
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_manifest_bad_path/evident.pkg
        --package
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_manifest_bad_path)
    add_cli_failure_compare_test_with_normalized_path(cli_package_manifest_absolute_path
        expected_cli_package_manifest_absolute_path.stderr.out
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_manifest_absolute_path/evident.pkg
        --package
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_manifest_absolute_path)
    add_cli_failure_compare_test_with_normalized_path(cli_package_manifest_missing_source
        expected_cli_package_manifest_missing_source.stderr.out
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_manifest_missing_source/evident.pkg
        --package
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_manifest_missing_source)
    add_cli_failure_compare_test_with_normalized_path(cli_package_manifest_wrong_extension
        expected_cli_package_manifest_wrong_extension.stderr.out
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_manifest_wrong_extension/evident.pkg
        --package
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_manifest_wrong_extension)
    add_cli_failure_compare_test_with_normalized_path(cli_package_manifest_duplicate
        expected_cli_package_manifest_duplicate.stderr.out
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_manifest_duplicate/evident.pkg
        --package
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/package_manifest_duplicate)
    add_emit_pe_executable_test(validate_exe_constant_pe_x64 native_main_constant.evd validate_native_main_constant.exe)
    add_emit_pe_executable_test(validate_exe_phase_construct_pe_x64 native_main_phase_construct.evd validate_native_phase_construct.exe)

    add_diagnostic_compare_test(diag_parse_missing_expression
        invalid_parse_missing_expression.evd
        expected_invalid_parse_missing_expression.diag.out)
    add_diagnostic_compare_test(diag_parse_missing_field_type
        invalid_parse_missing_field_type.evd
        expected_invalid_parse_missing_field_type.diag.out)
    add_diagnostic_compare_test(diag_parse_missing_field_comma
        invalid_parse_missing_field_comma.evd
        expected_invalid_parse_missing_field_comma.diag.out)
    add_diagnostic_compare_test(diag_parse_missing_param_colon
        invalid_parse_missing_param_colon.evd
        expected_invalid_parse_missing_param_colon.diag.out)
    add_diagnostic_compare_test(diag_parse_missing_param_comma
        invalid_parse_missing_param_comma.evd
        expected_invalid_parse_missing_param_comma.diag.out)
    add_diagnostic_compare_test(diag_parse_missing_variant_comma
        invalid_parse_missing_variant_comma.evd
        expected_invalid_parse_missing_variant_comma.diag.out)
    add_diagnostic_compare_test(diag_parse_missing_phase_position_comma
        invalid_parse_missing_phase_position_comma.evd
        expected_invalid_parse_missing_phase_position_comma.diag.out)
    add_diagnostic_compare_test(diag_parse_missing_match_arm_comma
        invalid_parse_missing_match_arm_comma.evd
        expected_invalid_parse_missing_match_arm_comma.diag.out)
    add_diagnostic_compare_test(diag_parse_missing_match_arrow
        invalid_parse_missing_match_arrow.evd
        expected_invalid_parse_missing_match_arrow.diag.out)
    add_diagnostic_compare_test(diag_parse_missing_block_close_before_decl
        invalid_parse_missing_block_close_before_decl.evd
        expected_invalid_parse_missing_block_close_before_decl.diag.out)
    add_diagnostic_compare_test(diag_parse_missing_module_kind
        invalid_parse_missing_module_kind.evd
        expected_invalid_parse_missing_module_kind.diag.out)
    add_diagnostic_compare_test(diag_parse_invalid_import_syntax
        invalid_parse_import_syntax.evd
        expected_invalid_parse_import_syntax.diag.out)
    add_diagnostic_compare_test(diag_parse_phase_shape
        invalid_parse_phase_shape.evd
        expected_invalid_parse_phase_shape.diag.out)
    add_diagnostic_compare_test(diag_parse_variant_payload_close
        invalid_parse_variant_payload_close.evd
        expected_invalid_parse_variant_payload_close.diag.out)
    add_diagnostic_compare_test(diag_semantic_phase_family_type
        invalid_phase_family_type.evd
        expected_invalid_phase_family_type.diag.out)
    add_diagnostic_compare_test(diag_semantic_type_reference_contracts
        invalid_type_reference_contracts.evd
        expected_invalid_type_reference_contracts.diag.out)
    add_diagnostic_compare_test(diag_semantic_builtin_collection_arity
        invalid_builtin_collection_arity.evd
        expected_invalid_builtin_collection_arity.diag.out)
    add_diagnostic_compare_test(diag_semantic_duplicate_phase_position
        invalid_duplicate_phase_position.evd
        expected_invalid_duplicate_phase_position.diag.out)
    add_diagnostic_compare_test(diag_semantic_declaration_shape_contracts
        invalid_declaration_shape_contracts.evd
        expected_invalid_declaration_shape_contracts.diag.out)
    add_diagnostic_compare_test(diag_semantic_unknown_type
        invalid_unknown_type.evd
        expected_invalid_unknown_type.diag.out)
    add_diagnostic_compare_test(diag_semantic_non_reason_yields
        invalid_non_reason_yields.evd
        expected_invalid_non_reason_yields.diag.out)
    add_diagnostic_compare_test(diag_semantic_reason_data_positions
        invalid_reason_data_positions.evd
        expected_invalid_reason_data_positions.diag.out)
    add_diagnostic_compare_test(diag_semantic_duplicate_decl
        invalid_duplicate_decl.evd
        expected_invalid_duplicate_decl.diag.out)
    add_diagnostic_compare_test(diag_semantic_empty_state
        invalid_empty_state.evd
        expected_invalid_empty_state.diag.out)
    add_diagnostic_compare_test(diag_semantic_pseudo_optional
        invalid_pseudo_optional.evd
        expected_invalid_pseudo_optional.diag.out)
    add_diagnostic_compare_test(diag_semantic_reserved_public_names
        invalid_reserved_names.evd
        expected_invalid_reserved_names.diag.out)
    add_diagnostic_compare_test(diag_parse_foreign_body
        invalid_foreign_body.evd
        expected_invalid_foreign_body.diag.out)
    add_diagnostic_compare_test(diag_parse_foreign_yields
        invalid_foreign_yields.evd
        expected_invalid_foreign_yields.diag.out)
    add_diagnostic_compare_test(diag_semantic_private_type_leak
        invalid_private_type_leak.evd
        expected_invalid_private_type_leak.diag.out)
    add_diagnostic_compare_test(diag_semantic_generic_private_type_leak_param
        invalid_generic_private_type_leak_param.evd
        expected_invalid_generic_private_type_leak_param.diag.out)
    add_diagnostic_compare_test(diag_semantic_generic_private_type_leak_field
        invalid_generic_private_type_leak_field.evd
        expected_invalid_generic_private_type_leak_field.diag.out)
    add_diagnostic_compare_test(diag_semantic_public_record_private_field_type
        invalid_public_record_private_field_type.evd
        expected_invalid_public_record_private_field_type.diag.out)
    add_diagnostic_compare_test(diag_semantic_public_proof_private_field_type
        invalid_public_proof_private_field_type.evd
        expected_invalid_public_proof_private_field_type.diag.out)
    add_diagnostic_compare_test(diag_semantic_public_phase_private_field_type
        invalid_public_phase_private_field_type.evd
        expected_invalid_public_phase_private_field_type.diag.out)
    add_diagnostic_compare_test(diag_semantic_public_state_private_payload
        invalid_public_state_private_payload.evd
        expected_invalid_public_state_private_payload.diag.out)
    add_diagnostic_compare_test(diag_semantic_public_reason_private_payload
        invalid_public_reason_private_payload.evd
        expected_invalid_public_reason_private_payload.diag.out)
    add_diagnostic_compare_test(diag_semantic_public_return_private_type
        invalid_public_return_private_type.evd
        expected_invalid_public_return_private_type.diag.out)
    add_diagnostic_compare_test(diag_semantic_public_permit_param_private_type
        invalid_public_permit_param_private_type.evd
        expected_invalid_public_permit_param_private_type.diag.out)
    add_diagnostic_compare_test(diag_semantic_public_fails_private_reason
        invalid_public_fails_private_reason.evd
        expected_invalid_public_fails_private_reason.diag.out)
    add_diagnostic_compare_test(diag_semantic_public_grants_private_permit
        invalid_public_grants_private_permit.evd
        expected_invalid_public_grants_private_permit.diag.out)
    add_diagnostic_compare_test(diag_semantic_public_proves_private_proof
        invalid_public_proves_private_proof.evd
        expected_invalid_public_proves_private_proof.diag.out)
    add_diagnostic_compare_test(diag_semantic_duplicate_proves_clause
        invalid_duplicate_proves_clause.evd
        expected_invalid_duplicate_proves_clause.diag.out)
    add_diagnostic_compare_test(diag_parse_duplicate_contract_clauses
        invalid_duplicate_contract_clauses.evd
        expected_invalid_duplicate_contract_clauses.diag.out)
    add_diagnostic_compare_test(diag_semantic_contract_clause_targets
        invalid_contract_clause_targets.evd
        expected_invalid_contract_clause_targets.diag.out)
    add_diagnostic_compare_test(diag_semantic_unhandled_failure
        invalid_yielded_call_unhandled.evd
        expected_invalid_yielded_call_unhandled.diag.out)
    add_diagnostic_compare_test(diag_semantic_fail_wrong_reason
        invalid_fail_wrong_reason.evd
        expected_invalid_fail_wrong_reason.diag.out)
    add_diagnostic_compare_test(diag_semantic_try_outside_yields
        invalid_try_outside_yields.evd
        expected_invalid_try_outside_yields.diag.out)
    add_diagnostic_compare_test(diag_semantic_try_fail_contracts
        invalid_try_fail_contracts.evd
        expected_invalid_try_fail_contracts.diag.out)
    add_diagnostic_compare_test(diag_semantic_fail_contracts
        invalid_fail_contracts.evd
        expected_invalid_fail_contracts.diag.out)
    add_diagnostic_compare_test(diag_semantic_direct_proof_construct
        invalid_direct_proof_construct.evd
        expected_invalid_direct_proof_construct.diag.out)
    add_diagnostic_compare_test(diag_semantic_prove_without_authority
        invalid_prove_without_authority.evd
        expected_invalid_prove_without_authority.diag.out)
    add_diagnostic_compare_test(diag_semantic_prove_wrong_proof
        invalid_prove_wrong_proof.evd
        expected_invalid_prove_wrong_proof.diag.out)
    add_diagnostic_compare_test(diag_semantic_prove_contracts
        invalid_prove_contracts.evd
        expected_invalid_prove_contracts.diag.out)
    add_diagnostic_compare_test(diag_semantic_field_access_private
        invalid_field_access_private.evd
        expected_invalid_field_access_private.diag.out)
    add_diagnostic_compare_test(diag_semantic_construct_private_field
        invalid_construct_private_field.evd
        expected_invalid_construct_private_field.diag.out)
    add_diagnostic_compare_test(diag_semantic_permit_argument_without_as
        invalid_permit_argument_without_as.evd
        expected_invalid_permit_argument_without_as.diag.out)
    add_diagnostic_compare_test(diag_semantic_ordinary_permit_param
        invalid_ordinary_permit_param.evd
        expected_invalid_ordinary_permit_param.diag.out)
    add_diagnostic_compare_test(diag_semantic_permit_generic_argument
        invalid_permit_generic_argument.evd
        expected_invalid_permit_generic_argument.diag.out)
    add_diagnostic_compare_test(diag_semantic_permit_data_positions
        invalid_permit_data_positions.evd
        expected_invalid_permit_data_positions.diag.out)
    add_diagnostic_compare_test(diag_semantic_grant_direct_call
        invalid_grant_direct_call.evd
        expected_invalid_grant_direct_call.diag.out)
    add_diagnostic_compare_test(diag_semantic_grant_contracts
        invalid_grant_contracts.evd
        expected_invalid_grant_contracts.diag.out)
    add_diagnostic_compare_test(diag_semantic_with_non_grant
        invalid_with_non_grant.evd
        expected_invalid_with_non_grant.diag.out)
    add_diagnostic_compare_test(diag_semantic_permit_escape_let
        invalid_permit_escape_let.evd
        expected_invalid_permit_escape_let.diag.out)
    add_diagnostic_compare_test(diag_semantic_missing_permit_argument
        invalid_missing_permit_argument.evd
        expected_invalid_missing_permit_argument.diag.out)
    add_diagnostic_compare_test(diag_semantic_grant_failure_unhandled
        invalid_grant_failure_unhandled.evd
        expected_invalid_grant_failure_unhandled.diag.out)
    add_diagnostic_compare_test(diag_semantic_cross_module_grants
        invalid_cross_module_grants.evd
        expected_invalid_cross_module_grants.diag.out)
    add_diagnostic_compare_test(diag_semantic_cross_module_proves
        invalid_cross_module_proves.evd
        expected_invalid_cross_module_proves.diag.out)
    add_diagnostic_compare_test(diag_semantic_grant_failure_mismatch
        invalid_grant_failure_mismatch.evd
        expected_invalid_grant_failure_mismatch.diag.out)
    add_diagnostic_compare_test(diag_semantic_external_phase_construct
        invalid_external_phase_construct.evd
        expected_invalid_external_phase_construct.diag.out)
    add_diagnostic_compare_test(diag_semantic_phase_reuse_after_move
        invalid_phase_reuse_after_move.evd
        expected_invalid_phase_reuse_after_move.diag.out)
    add_diagnostic_compare_test(diag_semantic_phase_match
        invalid_phase_match.evd
        expected_invalid_phase_match.diag.out)
    add_diagnostic_compare_test(diag_semantic_phase_expression_contracts
        invalid_phase_expression_contracts.evd
        expected_invalid_phase_expression_contracts.diag.out)
    add_diagnostic_compare_test(diag_semantic_construct_initializer_contracts
        invalid_construct_initializer_contracts.evd
        expected_invalid_construct_initializer_contracts.diag.out)
    add_diagnostic_compare_test(diag_semantic_expression_constructor_contracts
        invalid_expression_constructor_contracts.evd
        expected_invalid_expression_constructor_contracts.diag.out)
    add_diagnostic_compare_test(diag_semantic_call_contracts
        invalid_call_contracts.evd
        expected_invalid_call_contracts.diag.out)
    add_diagnostic_compare_test(diag_semantic_field_access_contracts
        invalid_field_access_contracts.evd
        expected_invalid_field_access_contracts.diag.out)
    add_diagnostic_compare_test(diag_semantic_affine_field_extract
        invalid_affine_field_extract.evd
        expected_invalid_affine_field_extract.diag.out)
    add_diagnostic_compare_test(diag_semantic_generic_affine_function_extract
        invalid_generic_affine_function_extract.evd
        expected_invalid_generic_affine_function_extract.diag.out)
    add_diagnostic_compare_test(diag_semantic_generic_affine_duplicate
        invalid_generic_affine_duplicate.evd
        expected_invalid_generic_affine_duplicate.diag.out)
    add_diagnostic_compare_test(diag_semantic_affine_move_note
        invalid_affine_after_let_move.evd
        expected_invalid_affine_after_let_move.diag.out)
    add_diagnostic_compare_test(diag_semantic_affine_reuse_argument
        invalid_affine_reuse_argument.evd
        expected_invalid_affine_reuse_argument.diag.out)
    add_diagnostic_compare_test(diag_semantic_affine_after_wrapper_move
        invalid_affine_after_wrapper_move.evd
        expected_invalid_affine_after_wrapper_move.diag.out)
    add_diagnostic_compare_test(diag_semantic_affine_generic_wrapper_move
        invalid_affine_generic_wrapper_move.evd
        expected_invalid_affine_generic_wrapper_move.diag.out)
    add_diagnostic_compare_test(diag_semantic_affine_after_match
        invalid_affine_after_match.evd
        expected_invalid_affine_after_match.diag.out)
    add_diagnostic_compare_test(diag_semantic_affine_branch_merge
        invalid_affine_branch_merge.evd
        expected_invalid_affine_branch_merge.diag.out)
    add_diagnostic_compare_test(diag_semantic_affine_match_payload_duplicate
        invalid_affine_match_payload_duplicate.evd
        expected_invalid_affine_match_payload_duplicate.diag.out)
    add_diagnostic_compare_test(diag_semantic_match_wildcard_multi_error
        invalid_match_wildcard.evd
        expected_invalid_match_wildcard.diag.out)
    add_diagnostic_compare_test(diag_semantic_match_payload_patterns
        invalid_match_payload_patterns.evd
        expected_invalid_match_payload_patterns.diag.out)
    add_diagnostic_compare_test(diag_semantic_match_contracts
        invalid_match_contracts.evd
        expected_invalid_match_contracts.diag.out)
    add_diagnostic_compare_test(diag_semantic_non_exhaustive_match
        invalid_non_exhaustive_match.evd
        expected_invalid_non_exhaustive_match.diag.out)
    add_diagnostic_compare_test(diag_semantic_match_non_state_subject
        invalid_match_non_state_subject.evd
        expected_invalid_match_non_state_subject.diag.out)
    add_diagnostic_compare_test(diag_semantic_failing_match_missing_success
        invalid_failing_match_missing_success.evd
        expected_invalid_failing_match_missing_success.diag.out)
    add_diagnostic_compare_test(diag_semantic_failing_match_missing_failure
        invalid_failing_match_missing_failure.evd
        expected_invalid_failing_match_missing_failure.diag.out)
    add_diagnostic_compare_test(diag_semantic_failing_match_duplicate_success
        invalid_failing_match_duplicate_success.evd
        expected_invalid_failing_match_duplicate_success.diag.out)
    add_diagnostic_compare_test(diag_semantic_failing_match_duplicate_failure
        invalid_failing_match_duplicate_failure.evd
        expected_invalid_failing_match_duplicate_failure.diag.out)
    add_diagnostic_compare_test(diag_semantic_failing_match_wrong_reason_variant
        invalid_failing_match_wrong_reason_variant.evd
        expected_invalid_failing_match_wrong_reason_variant.diag.out)
    add_diagnostic_compare_test(diag_semantic_failing_match_state_pattern
        invalid_failing_match_state_pattern.evd
        expected_invalid_failing_match_state_pattern.diag.out)
    add_diagnostic_compare_test(diag_semantic_failing_match_contracts
        invalid_failing_match_contracts.evd
        expected_invalid_failing_match_contracts.diag.out)
    add_diagnostic_compare_test(diag_semantic_generic_call_missing_args
        invalid_generic_call_missing_args.evd
        expected_invalid_generic_call_missing_args.diag.out)
    add_diagnostic_compare_test(diag_semantic_generic_call_wrong_arity
        invalid_generic_call_wrong_arity.evd
        expected_invalid_generic_call_wrong_arity.diag.out)
    add_diagnostic_compare_test(diag_semantic_generic_call_type_mismatch
        invalid_generic_call_type_mismatch.evd
        expected_invalid_generic_call_type_mismatch.diag.out)
    add_diagnostic_compare_test(diag_semantic_generic_contract_type_arguments
        invalid_generic_contract_type_arguments.evd
        expected_invalid_generic_contract_type_arguments.diag.out)
    add_diagnostic_compare_test(diag_semantic_generic_structural_blindness
        invalid_generic_structural_blindness.evd
        expected_invalid_generic_structural_blindness.diag.out)
    add_diagnostic_compare_test(diag_semantic_generic_record_construct_missing_args
        invalid_generic_record_construct_missing_args.evd
        expected_invalid_generic_record_construct_missing_args.diag.out)
    add_diagnostic_compare_test(diag_semantic_generic_record_construct_wrong_arity
        invalid_generic_record_construct_wrong_arity.evd
        expected_invalid_generic_record_construct_wrong_arity.diag.out)
    add_diagnostic_compare_test(diag_semantic_generic_record_construct_non_generic_args
        invalid_generic_record_construct_non_generic_args.evd
        expected_invalid_generic_record_construct_non_generic_args.diag.out)
    add_diagnostic_compare_test(diag_semantic_generic_record_construct_type_mismatch
        invalid_generic_record_construct_type_mismatch.evd
        expected_invalid_generic_record_construct_type_mismatch.diag.out)
    add_diagnostic_compare_test(diag_semantic_generic_record_type_missing_args
        invalid_generic_record_type_missing_args.evd
        expected_invalid_generic_record_type_missing_args.diag.out)
    add_diagnostic_compare_test(diag_semantic_domain_generic_record
        invalid_domain_generic_record.evd
        expected_invalid_domain_generic_record.diag.out)
    add_diagnostic_compare_test(diag_semantic_module_kind_generics
        invalid_module_kind_generics.evd
        expected_invalid_module_kind_generics.diag.out)
    add_diagnostic_compare_test(diag_semantic_hazard_generic_fn
        invalid_hazard_generic_fn.evd
        expected_invalid_hazard_generic_fn.diag.out)
    add_diagnostic_compare_test(diag_semantic_foundation_nested_module
        invalid_foundation_nested_module.evd
        expected_invalid_foundation_nested_module.diag.out)
    add_diagnostic_compare_test(diag_semantic_foundation_decl_kind
        invalid_foundation_decl_kind.evd
        expected_invalid_foundation_decl_kind.diag.out)
    add_diagnostic_compare_test(diag_parse_foreign_generic_fn
        invalid_foreign_generic_fn.evd
        expected_invalid_foreign_generic_fn.diag.out)
    add_diagnostic_compare_test(diag_parse_foreign_generics
        invalid_foreign_generics.evd
        expected_invalid_foreign_generics.diag.out)
    add_diagnostic_compare_test(diag_semantic_domain_foreign_fn
        invalid_domain_foreign_fn.evd
        expected_invalid_domain_foreign_fn.diag.out)
    add_diagnostic_compare_test(diag_parse_foreign_fn_body
        invalid_foreign_fn_body.evd
        expected_invalid_foreign_fn_body.diag.out)
    add_diagnostic_compare_test(diag_parse_foreign_fn_permit_param
        invalid_foreign_fn_permit_param.evd
        expected_invalid_foreign_fn_permit_param.diag.out)
    add_diagnostic_compare_test(diag_parse_foreign_fn_fails
        invalid_foreign_fn_fails.evd
        expected_invalid_foreign_fn_fails.diag.out)
    add_diagnostic_compare_test(diag_parse_foreign_fn_grants
        invalid_foreign_fn_grants.evd
        expected_invalid_foreign_fn_grants.diag.out)
    add_diagnostic_compare_test(diag_parse_foreign_fn_proves
        invalid_foreign_fn_proves.evd
        expected_invalid_foreign_fn_proves.diag.out)
    add_diagnostic_compare_test(diag_parse_foreign_proves
        invalid_foreign_proves.evd
        expected_invalid_foreign_proves.diag.out)
    add_diagnostic_compare_test(diag_parse_generic_state_decl
        invalid_generic_state_decl.evd
        expected_invalid_generic_state_decl.diag.out)
    add_diagnostic_compare_test(diag_parse_generic_reason_decl
        invalid_generic_reason_decl.evd
        expected_invalid_generic_reason_decl.diag.out)
    add_diagnostic_compare_test(diag_parse_generic_proof_decl
        invalid_generic_proof_decl.evd
        expected_invalid_generic_proof_decl.diag.out)
    add_diagnostic_compare_test(diag_parse_generic_permit_decl
        invalid_generic_permit_decl.evd
        expected_invalid_generic_permit_decl.diag.out)
    add_diagnostic_compare_test(diag_parse_generic_phase_decl
        invalid_generic_phase_decl.evd
        expected_invalid_generic_phase_decl.diag.out)
    add_backend_diagnostic_compare_test(diag_backend_missing_main
        valid_plain_call.evd
        expected_backend_missing_main.diag.out
        diag_backend_missing_main.exe)
    add_backend_diagnostic_compare_test(diag_backend_private_main
        native_invalid_main_private.evd
        expected_backend_private_main.diag.out
        diag_backend_private_main.exe)
    add_backend_diagnostic_compare_test(diag_backend_main_params
        native_invalid_main_params.evd
        expected_backend_main_params.diag.out
        diag_backend_main_params.exe)
    add_backend_diagnostic_compare_test(diag_backend_main_wrong_return
        native_invalid_main_wrong_return.evd
        expected_backend_main_wrong_return.diag.out
        diag_backend_main_wrong_return.exe)
    add_backend_diagnostic_compare_test(diag_backend_main_yields
        native_invalid_main_yields.evd
        expected_backend_main_yields.diag.out
        diag_backend_main_yields.exe)
    add_backend_diagnostic_compare_test(diag_backend_materialized_never
        native_invalid_materialized_never.evd
        expected_backend_materialized_never.diag.out
        diag_backend_materialized_never.exe)
    add_diagnostic_compare_test(diag_compiler_owned_collection_operation_contract
        native_invalid_compiler_owned_collection_operation.evd
        expected_backend_compiler_owned_collection_operation.diag.out)
    add_backend_target_diagnostic_compare_test(diag_backend_unsupported_target
        native_main_constant.evd
        expected_backend_unsupported_target.diag.out
        --emit-llvm
        diag_backend_unsupported_target.ll
        x86_64-unknown-linux-gnu)
    add_toolchain_diagnostic_compare_test(diag_toolchain_missing_clang
        native_main_constant.evd
        expected_toolchain_missing_clang.diag.out
        --emit-obj
        diag_toolchain_missing_clang.obj
        evident-missing-clang-for-diagnostic-test.exe)
    add_toolchain_diagnostic_compare_test(diag_toolchain_whitespace_clang_override
        native_main_constant.evd
        expected_toolchain_whitespace_clang.diag.out
        --emit-obj
        diag_toolchain_whitespace_clang.obj
        "${EVIDENT_WHITESPACE_CLANG_OVERRIDE}")
    add_multi_diagnostic_compare_test(diag_package_split_duplicate_member
        expected_package_split_duplicate_member.diag.out
        invalid_package_split_duplicate_member_first.evd
        invalid_package_split_duplicate_member_second.evd)
    add_multi_diagnostic_compare_test(diag_package_split_kind_mismatch
        expected_package_split_kind.diag.out
        invalid_package_split_kind_first.evd
        invalid_package_split_kind_second.evd)
    add_multi_diagnostic_compare_test(diag_package_private_type_leak
        expected_package_private_type_leak.diag.out
        package_private_type_source.evd
        package_private_type_leak.evd)
    add_multi_diagnostic_compare_test(diag_package_private_field_construct
        expected_package_private_field_construct.diag.out
        package_private_field_source.evd
        package_private_field_consumer.evd)
    add_multi_diagnostic_compare_test(diag_package_private_proof_field_access
        expected_package_private_proof_field_access.diag.out
        package_proof_private_field_source.evd
        package_proof_private_field_consumer.evd)
    add_multi_diagnostic_compare_test(diag_package_private_phase_field_access
        expected_package_private_phase_field_access.diag.out
        package_phase_private_field_source.evd
        package_phase_private_field_consumer.evd)
    add_multi_diagnostic_compare_test(diag_package_cross_file_authority
        expected_package_cross_file_authority.diag.out
        package_authority_source.evd
        package_authority_consumer.evd)
    add_diagnostic_compare_test(diag_package_import_unknown
        invalid_package_import_unknown.evd
        expected_package_import_unknown.diag.out)
    add_multi_diagnostic_compare_test(diag_package_import_non_module
        expected_package_import_non_module.diag.out
        package_support.evd
        invalid_package_import_non_module.evd)
    add_diagnostic_compare_test(diag_package_import_duplicate
        invalid_package_import_duplicate.evd
        expected_package_import_duplicate.diag.out)
    add_multi_diagnostic_compare_test(diag_package_import_gate_missing_type
        expected_package_import_gate_missing_type.diag.out
        package_support.evd
        invalid_package_import_gate_missing_type.evd)
    add_multi_diagnostic_compare_test(diag_package_import_gate_missing_call
        expected_package_import_gate_missing_call.diag.out
        package_import_value_support.evd
        invalid_package_import_gate_missing_call.evd)
    add_multi_diagnostic_compare_test(diag_package_import_gate_generic_args
        expected_package_import_gate_generic_args.diag.out
        package_support.evd
        package_generic_gate_support.evd
        invalid_package_import_gate_generic_args.evd)
    add_multi_diagnostic_compare_test(diag_package_import_file_local_missing
        expected_package_import_file_local_missing.diag.out
        package_import_value_support.evd
        package_import_gated_consumer.evd
        invalid_package_import_file_local_missing.evd)

    add_reject_test(reject_reserved_public_names invalid_reserved_names.evd)
    add_reject_test(reject_public_private_leak invalid_private_type_leak.evd)
    add_reject_test(reject_non_reason_yields invalid_non_reason_yields.evd)
    add_reject_test(reject_reason_data_positions invalid_reason_data_positions.evd)
    add_reject_test(reject_duplicate_decl invalid_duplicate_decl.evd)
    add_reject_test(reject_declaration_shape_contracts invalid_declaration_shape_contracts.evd)
    add_reject_test(reject_empty_state invalid_empty_state.evd)
    add_reject_test(reject_pseudo_optional invalid_pseudo_optional.evd)
    add_reject_test(reject_foreign_body invalid_foreign_body.evd)
    add_reject_test(reject_foreign_yields invalid_foreign_yields.evd)
    add_reject_test(reject_unknown_type invalid_unknown_type.evd)
    add_reject_test(reject_type_reference_contracts invalid_type_reference_contracts.evd)
    add_reject_test(reject_non_exhaustive_match invalid_non_exhaustive_match.evd)
    add_reject_test(reject_match_non_state_subject invalid_match_non_state_subject.evd)
    add_reject_test(reject_match_payload_patterns invalid_match_payload_patterns.evd)
    add_reject_test(reject_match_contracts invalid_match_contracts.evd)
    add_reject_test(reject_failing_match_missing_success invalid_failing_match_missing_success.evd)
    add_reject_test(reject_failing_match_missing_failure invalid_failing_match_missing_failure.evd)
    add_reject_test(reject_failing_match_duplicate_success invalid_failing_match_duplicate_success.evd)
    add_reject_test(reject_failing_match_duplicate_failure invalid_failing_match_duplicate_failure.evd)
    add_reject_test(reject_failing_match_wrong_reason_variant invalid_failing_match_wrong_reason_variant.evd)
    add_reject_test(reject_failing_match_state_pattern invalid_failing_match_state_pattern.evd)
    add_reject_test(reject_failing_match_contracts invalid_failing_match_contracts.evd)
    add_reject_test(reject_unhandled_yield invalid_yielded_call_unhandled.evd)
    add_reject_test(reject_try_outside_yields invalid_try_outside_yields.evd)
    add_reject_test(reject_try_fail_contracts invalid_try_fail_contracts.evd)
    add_reject_test(reject_fail_wrong_reason invalid_fail_wrong_reason.evd)
    add_reject_test(reject_fail_contracts invalid_fail_contracts.evd)
    add_reject_test(reject_match_wildcard invalid_match_wildcard.evd)
    add_reject_test(reject_direct_proof_construct invalid_direct_proof_construct.evd)
    add_reject_test(reject_prove_without_authority invalid_prove_without_authority.evd)
    add_reject_test(reject_duplicate_proves_clause invalid_duplicate_proves_clause.evd)
    add_reject_test(reject_duplicate_contract_clauses invalid_duplicate_contract_clauses.evd)
    add_reject_test(reject_contract_clause_targets invalid_contract_clause_targets.evd)
    add_reject_test(reject_prove_wrong_proof invalid_prove_wrong_proof.evd)
    add_reject_test(reject_prove_contracts invalid_prove_contracts.evd)
    add_reject_test(reject_foreign_proves invalid_foreign_proves.evd)
    add_reject_test(reject_foreign_generics invalid_foreign_generics.evd)
    add_reject_test(reject_generic_contract_type_arguments invalid_generic_contract_type_arguments.evd)
    add_reject_test(reject_generic_structural_blindness invalid_generic_structural_blindness.evd)
    add_reject_test(reject_module_kind_generics invalid_module_kind_generics.evd)
    add_reject_test(reject_builtin_collection_arity invalid_builtin_collection_arity.evd)
    add_reject_test(reject_field_access_private invalid_field_access_private.evd)
    add_reject_test(reject_construct_private_field invalid_construct_private_field.evd)
    add_reject_test(reject_grant_direct_call invalid_grant_direct_call.evd)
    add_reject_test(reject_grant_contracts invalid_grant_contracts.evd)
    add_reject_test(reject_with_non_grant invalid_with_non_grant.evd)
    add_reject_test(reject_permit_escape_let invalid_permit_escape_let.evd)
    add_reject_test(reject_missing_permit_argument invalid_missing_permit_argument.evd)
    add_reject_test(reject_ordinary_permit_param invalid_ordinary_permit_param.evd)
    add_reject_test(reject_permit_argument_without_as invalid_permit_argument_without_as.evd)
    add_reject_test(reject_permit_generic_argument invalid_permit_generic_argument.evd)
    add_reject_test(reject_permit_data_positions invalid_permit_data_positions.evd)
    add_reject_test(reject_grant_failure_unhandled invalid_grant_failure_unhandled.evd)
    add_reject_test(reject_grant_failure_mismatch invalid_grant_failure_mismatch.evd)
    add_reject_test(reject_cross_module_grants invalid_cross_module_grants.evd)
    add_reject_test(reject_cross_module_proves invalid_cross_module_proves.evd)
    add_reject_test(reject_foundation_nested_module invalid_foundation_nested_module.evd)
    add_reject_test(reject_foundation_decl_kind invalid_foundation_decl_kind.evd)
    add_reject_test(reject_external_phase_construct invalid_external_phase_construct.evd)
    add_reject_test(reject_phase_family_type invalid_phase_family_type.evd)
    add_reject_test(reject_phase_match invalid_phase_match.evd)
    add_reject_test(reject_phase_expression_contracts invalid_phase_expression_contracts.evd)
    add_reject_test(reject_construct_initializer_contracts invalid_construct_initializer_contracts.evd)
    add_reject_test(reject_expression_constructor_contracts invalid_expression_constructor_contracts.evd)
    add_reject_test(reject_call_contracts invalid_call_contracts.evd)
    add_reject_test(reject_field_access_contracts invalid_field_access_contracts.evd)
    add_reject_test(reject_phase_reuse_after_move invalid_phase_reuse_after_move.evd)
    add_reject_test(reject_affine_field_extract invalid_affine_field_extract.evd)
    add_reject_test(reject_generic_affine_function_extract invalid_generic_affine_function_extract.evd)
    add_reject_test(reject_generic_affine_duplicate invalid_generic_affine_duplicate.evd)
    add_reject_test(reject_affine_reuse_argument invalid_affine_reuse_argument.evd)
    add_reject_test(reject_affine_after_let_move invalid_affine_after_let_move.evd)
    add_reject_test(reject_affine_after_wrapper_move invalid_affine_after_wrapper_move.evd)
    add_reject_test(reject_affine_generic_wrapper_move invalid_affine_generic_wrapper_move.evd)
    add_reject_test(reject_affine_after_match invalid_affine_after_match.evd)
    add_reject_test(reject_affine_branch_merge invalid_affine_branch_merge.evd)
    add_reject_test(reject_affine_match_payload_duplicate invalid_affine_match_payload_duplicate.evd)

    add_backend_reject_test(reject_backend_missing_main valid_plain_call.evd reject_backend_missing_main.exe)
    add_backend_reject_test(reject_backend_private_main native_invalid_main_private.evd reject_backend_private_main.exe)
    add_backend_reject_test(reject_backend_main_params native_invalid_main_params.evd reject_backend_main_params.exe)
    add_backend_reject_test(reject_backend_main_wrong_return native_invalid_main_wrong_return.evd reject_backend_main_wrong_return.exe)
    add_backend_reject_test(reject_backend_main_yields native_invalid_main_yields.evd reject_backend_main_yields.exe)
    add_emit_exe_test(run_native_empty_collections native_main_empty_collections.evd 7 native_empty_collections.exe)
    add_emit_exe_test(run_native_builtin_cstring_map native_main_builtin_cstring_map.evd 7 native_builtin_cstring_map.exe)
endif()
