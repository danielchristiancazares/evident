if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

function(run_release_docs_validation validation_name source_dir should_pass)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE_DIR=${source_dir}"
            "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseDocs.cmake"
        RESULT_VARIABLE validation_result
        OUTPUT_VARIABLE validation_stdout
        ERROR_VARIABLE validation_stderr
    )

    if(should_pass)
        if(NOT validation_result EQUAL 0)
            message(FATAL_ERROR
                "${validation_name} release-docs validation failed with exit code ${validation_result}\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
        endif()
        return()
    endif()

    if(validation_result EQUAL 0)
        message(FATAL_ERROR
            "${validation_name} release-docs validation unexpectedly accepted stale docs\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
    endif()

    set(actual_error "${validation_stderr}")
    string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
    string(REPLACE "\r" "\n" actual_error "${actual_error}")

    foreach(required_fragment IN LISTS ARGN)
        string(FIND "${actual_error}" "${required_fragment}" fragment_index)
        if(fragment_index EQUAL -1)
            message(FATAL_ERROR
                "${validation_name} release-docs validation error did not contain required fragment '${required_fragment}'\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
        endif()
    endforeach()
endfunction()

function(copy_release_docs_fixture fixture_dir)
    file(MAKE_DIRECTORY "${fixture_dir}")
    file(COPY "${SOURCE_DIR}/README.md" DESTINATION "${fixture_dir}")
    file(COPY "${SOURCE_DIR}/.gitignore" DESTINATION "${fixture_dir}")
    file(COPY "${SOURCE_DIR}/CMakePresets.json" DESTINATION "${fixture_dir}")
    file(COPY "${SOURCE_DIR}/docs" DESTINATION "${fixture_dir}" FILES_MATCHING PATTERN "*.md")
endfunction()

run_release_docs_validation("current tree" "${SOURCE_DIR}" TRUE)

string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" test_suffix)
set(work_dir "${BUILD_DIR}/release-docs-${test_suffix}")
set(valid_fixture_dir "${work_dir}/valid")
set(missing_matrix_fixture_dir "${work_dir}/missing-matrix")
set(stale_gitignore_fixture_dir "${work_dir}/stale-gitignore")
set(stale_presets_fixture_dir "${work_dir}/stale-presets")
set(stale_test_count_fixture_dir "${work_dir}/stale-test-count")
set(stale_readme_test_count_fixture_dir "${work_dir}/stale-readme-test-count")
set(stale_roadmap_fixture_dir "${work_dir}/stale-roadmap")
set(stale_source_tree_audit_fixture_dir "${work_dir}/stale-source-tree-audit")
set(stale_ci_source_tree_audit_fixture_dir "${work_dir}/stale-ci-source-tree-audit")
set(stale_native_backend_ci_fixture_dir "${work_dir}/stale-native-backend-ci")
set(stale_readme_action_pinning_fixture_dir "${work_dir}/stale-readme-action-pinning")
set(stale_readme_checkout_credentials_fixture_dir "${work_dir}/stale-readme-checkout-credentials")
set(stale_readme_checksum_fixture_dir "${work_dir}/stale-readme-checksum")
set(stale_readme_checksum_path_fixture_dir "${work_dir}/stale-readme-checksum-path")
set(stale_readme_checksum_zip_fixture_dir "${work_dir}/stale-readme-checksum-zip")
set(stale_readme_checksum_pair_fixture_dir "${work_dir}/stale-readme-checksum-pair")
set(stale_readme_bare_root_fixture_dir "${work_dir}/stale-readme-bare-root")
set(stale_readme_expected_allowlist_fixture_dir "${work_dir}/stale-readme-expected-allowlist")
set(stale_readme_empty_component_fixture_dir "${work_dir}/stale-readme-empty-component")
set(stale_readme_install_allowlist_fixture_dir "${work_dir}/stale-readme-install-allowlist")

copy_release_docs_fixture("${valid_fixture_dir}")
run_release_docs_validation("copied fixture" "${valid_fixture_dir}" TRUE)

copy_release_docs_fixture("${missing_matrix_fixture_dir}")
set(missing_matrix_readme "${missing_matrix_fixture_dir}/README.md")
file(READ "${missing_matrix_readme}" readme_text)
string(REPLACE "windows-2022" "windows-latest" readme_text "${readme_text}")
file(WRITE "${missing_matrix_readme}" "${readme_text}")
run_release_docs_validation(
    "missing matrix"
    "${missing_matrix_fixture_dir}"
    FALSE
    "release document README.md is missing expected text 'windows-2022'"
)

copy_release_docs_fixture("${stale_gitignore_fixture_dir}")
set(stale_gitignore_path "${stale_gitignore_fixture_dir}/.gitignore")
file(READ "${stale_gitignore_path}" gitignore_text)
string(REPLACE "build-*/\n" "" gitignore_text "${gitignore_text}")
file(WRITE "${stale_gitignore_path}" "${gitignore_text}")
run_release_docs_validation(
    "stale gitignore"
    "${stale_gitignore_fixture_dir}"
    FALSE
    "release document .gitignore is missing expected text 'build-*/'"
)

copy_release_docs_fixture("${stale_presets_fixture_dir}")
set(stale_presets_path "${stale_presets_fixture_dir}/CMakePresets.json")
file(READ "${stale_presets_path}" presets_text)
string(REPLACE
    "\"package_checksum\""
    "\"package\""
    presets_text
    "${presets_text}"
)
file(WRITE "${stale_presets_path}" "${presets_text}")
run_release_docs_validation(
    "stale presets"
    "${stale_presets_fixture_dir}"
    FALSE
    "release document CMakePresets.json is missing expected text"
    "\"package_checksum\""
)

copy_release_docs_fixture("${stale_test_count_fixture_dir}")
set(stale_test_count_finish_plan "${stale_test_count_fixture_dir}/docs/COMPILER_FINISH_PLAN.md")
file(READ "${stale_test_count_finish_plan}" finish_plan_text)
string(REPLACE "377/377" "360/360" finish_plan_text "${finish_plan_text}")
file(WRITE "${stale_test_count_finish_plan}" "${finish_plan_text}")
run_release_docs_validation(
    "stale test count"
    "${stale_test_count_fixture_dir}"
    FALSE
    "release document docs/COMPILER_FINISH_PLAN.md is missing expected text"
    "passed `377/377` tests."
)

copy_release_docs_fixture("${stale_readme_test_count_fixture_dir}")
set(stale_readme_test_count_path "${stale_readme_test_count_fixture_dir}/README.md")
file(READ "${stale_readme_test_count_path}" readme_text)
string(REPLACE
    "current `377/377` CTest pass summary inside `[ctest output]`"
    "current `361/361` CTest pass summary inside `[ctest output]`"
    readme_text
    "${readme_text}"
)
file(WRITE "${stale_readme_test_count_path}" "${readme_text}")
run_release_docs_validation(
    "stale README test count"
    "${stale_readme_test_count_fixture_dir}"
    FALSE
    "release document README.md is missing expected text"
    "377/377"
    "CTest pass summary"
)

copy_release_docs_fixture("${stale_source_tree_audit_fixture_dir}")
set(stale_release_checklist "${stale_source_tree_audit_fixture_dir}/docs/RELEASE_CHECKLIST.md")
file(READ "${stale_release_checklist}" release_checklist_text)
string(REPLACE
    "   cmake -DSOURCE_DIR=. -P cmake/AssertReleaseSourceTree.cmake\n"
    ""
    release_checklist_text
    "${release_checklist_text}"
)
file(WRITE "${stale_release_checklist}" "${release_checklist_text}")
run_release_docs_validation(
    "stale source-tree audit"
    "${stale_source_tree_audit_fixture_dir}"
    FALSE
    "release document docs/RELEASE_CHECKLIST.md is missing expected text"
    "AssertReleaseSourceTree.cmake"
)

copy_release_docs_fixture("${stale_ci_source_tree_audit_fixture_dir}")
set(stale_ci_source_tree_audit_readme "${stale_ci_source_tree_audit_fixture_dir}/README.md")
file(READ "${stale_ci_source_tree_audit_readme}" readme_text)
string(REPLACE
    "It runs the read-only release source-tree audit before configure/build/test, "
    ""
    readme_text
    "${readme_text}"
)
file(WRITE "${stale_ci_source_tree_audit_readme}" "${readme_text}")
run_release_docs_validation(
    "stale CI source-tree audit"
    "${stale_ci_source_tree_audit_fixture_dir}"
    FALSE
    "release document README.md is missing expected text"
    "release source-tree audit"
)

copy_release_docs_fixture("${stale_native_backend_ci_fixture_dir}")
set(stale_native_backend_plan "${stale_native_backend_ci_fixture_dir}/docs/NATIVE_BACKEND_PLAN.md")
file(READ "${stale_native_backend_plan}" native_backend_text)
string(REPLACE
    "It runs the read-only release source-tree audit before configure/build/test, "
    ""
    native_backend_text
    "${native_backend_text}"
)
file(WRITE "${stale_native_backend_plan}" "${native_backend_text}")
run_release_docs_validation(
    "stale native backend CI source-tree audit"
    "${stale_native_backend_ci_fixture_dir}"
    FALSE
    "release document docs/NATIVE_BACKEND_PLAN.md is missing expected text"
    "release source-tree audit"
)

copy_release_docs_fixture("${stale_readme_action_pinning_fixture_dir}")
set(stale_readme_action_pinning_path "${stale_readme_action_pinning_fixture_dir}/README.md")
file(READ "${stale_readme_action_pinning_path}" readme_text)
string(REPLACE
    "Every remote action reference used by that workflow is pinned to a full commit SHA, "
    ""
    readme_text
    "${readme_text}"
)
file(WRITE "${stale_readme_action_pinning_path}" "${readme_text}")
run_release_docs_validation(
    "stale README action pinning"
    "${stale_readme_action_pinning_fixture_dir}"
    FALSE
    "release document README.md is missing expected text"
    "Every remote action"
)

copy_release_docs_fixture("${stale_readme_checkout_credentials_fixture_dir}")
set(stale_readme_checkout_credentials_path "${stale_readme_checkout_credentials_fixture_dir}/README.md")
file(READ "${stale_readme_checkout_credentials_path}" readme_text)
string(REPLACE
    "each checkout step sets `persist-credentials: false`, and "
    ""
    readme_text
    "${readme_text}"
)
file(WRITE "${stale_readme_checkout_credentials_path}" "${readme_text}")
run_release_docs_validation(
    "stale README checkout credentials"
    "${stale_readme_checkout_credentials_fixture_dir}"
    FALSE
    "release document README.md is missing expected text"
    "persist-credentials"
)

copy_release_docs_fixture("${stale_readme_checksum_fixture_dir}")
set(stale_readme_checksum_path "${stale_readme_checksum_fixture_dir}/README.md")
file(READ "${stale_readme_checksum_path}" readme_text)
string(REPLACE
    ", sidecars missing the final newline, and sidecars with extra trailing blank lines"
    ""
    readme_text
    "${readme_text}"
)
file(WRITE "${stale_readme_checksum_path}" "${readme_text}")
run_release_docs_validation(
    "stale README checksum sidecar coverage"
    "${stale_readme_checksum_fixture_dir}"
    FALSE
    "release document README.md is missing expected text"
    "sidecars missing"
)

copy_release_docs_fixture("${stale_readme_checksum_path_fixture_dir}")
set(stale_readme_checksum_path_doc "${stale_readme_checksum_path_fixture_dir}/README.md")
file(READ "${stale_readme_checksum_path_doc}" readme_text)
string(REPLACE
    "and the sidecar path to be exactly the package path plus `.sha256`, then "
    ""
    readme_text
    "${readme_text}"
)
file(WRITE "${stale_readme_checksum_path_doc}" "${readme_text}")
run_release_docs_validation(
    "stale README checksum path binding"
    "${stale_readme_checksum_path_fixture_dir}"
    FALSE
    "release document README.md is missing expected text"
    "sidecar path"
)

copy_release_docs_fixture("${stale_readme_checksum_zip_fixture_dir}")
set(stale_readme_checksum_zip_path "${stale_readme_checksum_zip_fixture_dir}/README.md")
file(READ "${stale_readme_checksum_zip_path}" readme_text)
string(REPLACE
    "requires the package path to name a `.zip` archive and "
    "requires "
    readme_text
    "${readme_text}"
)
file(WRITE "${stale_readme_checksum_zip_path}" "${readme_text}")
run_release_docs_validation(
    "stale README checksum ZIP path coverage"
    "${stale_readme_checksum_zip_fixture_dir}"
    FALSE
    "release document README.md is missing expected text"
    "package path"
)

copy_release_docs_fixture("${stale_readme_checksum_pair_fixture_dir}")
set(stale_readme_checksum_pair_path "${stale_readme_checksum_pair_fixture_dir}/README.md")
file(READ "${stale_readme_checksum_pair_path}" readme_text)
string(REPLACE
    "validates that pair with the shared package checksum validator so the sidecar path, canonical text, filename, and ZIP bytes match, "
    ""
    readme_text
    "${readme_text}"
)
file(WRITE "${stale_readme_checksum_pair_path}" "${readme_text}")
run_release_docs_validation(
    "stale README checksum pair binding"
    "${stale_readme_checksum_pair_fixture_dir}"
    FALSE
    "release document README.md is missing expected text"
    "shared package"
)

copy_release_docs_fixture("${stale_readme_bare_root_fixture_dir}")
set(stale_readme_bare_root_path "${stale_readme_bare_root_fixture_dir}/README.md")
file(READ "${stale_readme_bare_root_path}" readme_text)
string(REPLACE
    "bare package-root entries without a trailing slash, "
    ""
    readme_text
    "${readme_text}"
)
file(WRITE "${stale_readme_bare_root_path}" "${readme_text}")
run_release_docs_validation(
    "stale README bare package root coverage"
    "${stale_readme_bare_root_fixture_dir}"
    FALSE
    "release document README.md is missing expected text"
    "bare package-root"
)

copy_release_docs_fixture("${stale_readme_expected_allowlist_fixture_dir}")
set(stale_readme_expected_allowlist_path "${stale_readme_expected_allowlist_fixture_dir}/README.md")
file(READ "${stale_readme_expected_allowlist_path}" readme_text)
string(REPLACE
    "rejects unsafe, duplicate, or out-of-root expected package-entry allowlist paths, then "
    ""
    readme_text
    "${readme_text}"
)
file(WRITE "${stale_readme_expected_allowlist_path}" "${readme_text}")
run_release_docs_validation(
    "stale README expected allowlist coverage"
    "${stale_readme_expected_allowlist_fixture_dir}"
    FALSE
    "release document README.md is missing expected text"
    "expected package-entry allowlist"
)

copy_release_docs_fixture("${stale_readme_empty_component_fixture_dir}")
set(stale_readme_empty_component_path "${stale_readme_empty_component_fixture_dir}/README.md")
file(READ "${stale_readme_empty_component_path}" readme_text)
string(REPLACE
    "empty path components, "
    ""
    readme_text
    "${readme_text}"
)
file(WRITE "${stale_readme_empty_component_path}" "${readme_text}")
run_release_docs_validation(
    "stale README empty path component coverage"
    "${stale_readme_empty_component_fixture_dir}"
    FALSE
    "release document README.md is missing expected text"
    "empty path components"
)

copy_release_docs_fixture("${stale_readme_install_allowlist_fixture_dir}")
set(stale_readme_install_allowlist_path "${stale_readme_install_allowlist_fixture_dir}/README.md")
file(READ "${stale_readme_install_allowlist_path}" readme_text)
string(REPLACE
    "rejects unsafe or duplicate expected install allowlist paths, "
    ""
    readme_text
    "${readme_text}"
)
file(WRITE "${stale_readme_install_allowlist_path}" "${readme_text}")
run_release_docs_validation(
    "stale README install allowlist coverage"
    "${stale_readme_install_allowlist_fixture_dir}"
    FALSE
    "release document README.md is missing expected text"
    "expected install allowlist"
)

copy_release_docs_fixture("${stale_roadmap_fixture_dir}")
set(stale_finish_plan "${stale_roadmap_fixture_dir}/docs/COMPILER_FINISH_PLAN.md")
file(APPEND "${stale_finish_plan}" "\n- Decide and document the supported host/target matrix for the subset release.\n")
run_release_docs_validation(
    "stale roadmap"
    "${stale_roadmap_fixture_dir}"
    FALSE
    "release document docs/COMPILER_FINISH_PLAN.md still contains stale text"
    "Decide and document the supported host/target matrix"
)
