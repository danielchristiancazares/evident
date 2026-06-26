if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

if(NOT DEFINED WORKFLOW_PATH)
    message(FATAL_ERROR "WORKFLOW_PATH is required")
endif()

if(NOT EXISTS "${WORKFLOW_PATH}")
    message(FATAL_ERROR "expected CI workflow file does not exist: ${WORKFLOW_PATH}")
endif()

function(run_ci_workflow_validation validation_name workflow_path should_pass)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DWORKFLOW_PATH=${workflow_path}"
            "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertCiWorkflow.cmake"
        RESULT_VARIABLE validation_result
        OUTPUT_VARIABLE validation_stdout
        ERROR_VARIABLE validation_stderr
    )

    if(should_pass)
        if(NOT validation_result EQUAL 0)
            message(FATAL_ERROR
                "${validation_name} CI workflow validation failed with exit code ${validation_result}\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
        endif()
        return()
    endif()

    if(validation_result EQUAL 0)
        message(FATAL_ERROR
            "${validation_name} CI workflow validation unexpectedly accepted stale workflow text\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
    endif()

    set(actual_error "${validation_stderr}")
    string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
    string(REPLACE "\r" "\n" actual_error "${actual_error}")

    foreach(required_fragment IN LISTS ARGN)
        string(FIND "${actual_error}" "${required_fragment}" fragment_index)
        if(fragment_index EQUAL -1)
            message(FATAL_ERROR
                "${validation_name} CI workflow validation error did not contain required fragment '${required_fragment}'\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
        endif()
    endforeach()
endfunction()

run_ci_workflow_validation("current tree" "${WORKFLOW_PATH}" TRUE)

file(READ "${WORKFLOW_PATH}" workflow_text)
string(REPLACE "\r\n" "\n" workflow_text "${workflow_text}")
string(REPLACE "\r" "\n" workflow_text "${workflow_text}")

string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" test_suffix)
set(work_dir "${BUILD_DIR}/ci-workflow-${test_suffix}")
file(MAKE_DIRECTORY "${work_dir}")

set(mutable_action_workflow_path "${work_dir}/mutable-action-ci.yml")
set(mutable_action_workflow_text "${workflow_text}")
string(REPLACE
    "actions/checkout@34e114876b0b11c390a56381ad16ebd13914f8d5"
    "actions/checkout@v4"
    mutable_action_workflow_text
    "${mutable_action_workflow_text}"
)
if(mutable_action_workflow_text STREQUAL workflow_text)
    message(FATAL_ERROR "mutable-action fixture did not modify the CI workflow text")
endif()
file(WRITE "${mutable_action_workflow_path}" "${mutable_action_workflow_text}")
run_ci_workflow_validation(
    "mutable action"
    "${mutable_action_workflow_path}"
    FALSE
    "full 40-character commit"
    "actions/checkout@v4"
)

set(mutable_third_party_action_workflow_path "${work_dir}/mutable-third-party-action-ci.yml")
set(mutable_third_party_action_workflow_text "${workflow_text}")
string(REPLACE
    "      - name: Validate release source tree\n"
    "      - name: Mutable third-party action\n        uses: github/codeql-action/upload-sarif@v3\n\n      - name: Validate release source tree\n"
    mutable_third_party_action_workflow_text
    "${mutable_third_party_action_workflow_text}"
)
if(mutable_third_party_action_workflow_text STREQUAL workflow_text)
    message(FATAL_ERROR "mutable-third-party-action fixture did not modify the CI workflow text")
endif()
file(WRITE "${mutable_third_party_action_workflow_path}" "${mutable_third_party_action_workflow_text}")
run_ci_workflow_validation(
    "mutable third-party action"
    "${mutable_third_party_action_workflow_path}"
    FALSE
    "full 40-character commit"
    "github/codeql-action/upload-sarif@v3"
)

set(persisted_checkout_credentials_workflow_path "${work_dir}/persisted-checkout-credentials-ci.yml")
set(persisted_checkout_credentials_workflow_text "${workflow_text}")
string(REPLACE
    "persist-credentials: false"
    "persist-credentials: true"
    persisted_checkout_credentials_workflow_text
    "${persisted_checkout_credentials_workflow_text}"
)
if(persisted_checkout_credentials_workflow_text STREQUAL workflow_text)
    message(FATAL_ERROR "persisted-checkout-credentials fixture did not modify the CI workflow text")
endif()
file(WRITE "${persisted_checkout_credentials_workflow_path}" "${persisted_checkout_credentials_workflow_text}")
run_ci_workflow_validation(
    "persisted checkout credentials"
    "${persisted_checkout_credentials_workflow_path}"
    FALSE
    "persist-credentials: false"
)

set(top_level_write_permission_workflow_path "${work_dir}/top-level-write-permission-ci.yml")
set(top_level_write_permission_workflow_text "${workflow_text}")
string(REPLACE
    "permissions:\n  contents: read\n"
    "permissions:\n  contents: write\n"
    top_level_write_permission_workflow_text
    "${top_level_write_permission_workflow_text}"
)
if(top_level_write_permission_workflow_text STREQUAL workflow_text)
    message(FATAL_ERROR "top-level-write-permission fixture did not modify the CI workflow text")
endif()
file(WRITE "${top_level_write_permission_workflow_path}" "${top_level_write_permission_workflow_text}")
run_ci_workflow_validation(
    "top-level write permission"
    "${top_level_write_permission_workflow_path}"
    FALSE
    "top-level GITHUB_TOKEN permissions"
    "exactly contents"
)

set(extra_write_permission_workflow_path "${work_dir}/extra-write-permission-ci.yml")
set(extra_write_permission_workflow_text "${workflow_text}")
string(REPLACE
    "      id-token: write\n"
    "      id-token: write\n      packages: write\n"
    extra_write_permission_workflow_text
    "${extra_write_permission_workflow_text}"
)
if(extra_write_permission_workflow_text STREQUAL workflow_text)
    message(FATAL_ERROR "extra-write-permission fixture did not modify the CI workflow text")
endif()
file(WRITE "${extra_write_permission_workflow_path}" "${extra_write_permission_workflow_text}")
run_ci_workflow_validation(
    "extra write permission"
    "${extra_write_permission_workflow_path}"
    FALSE
    "unexpected write permission"
    "packages: write"
)

set(missing_manual_dispatch_workflow_path "${work_dir}/missing-manual-dispatch-ci.yml")
set(missing_manual_dispatch_workflow_text "${workflow_text}")
string(REPLACE
    "  workflow_dispatch:\n"
    ""
    missing_manual_dispatch_workflow_text
    "${missing_manual_dispatch_workflow_text}"
)
if(missing_manual_dispatch_workflow_text STREQUAL workflow_text)
    message(FATAL_ERROR "missing-manual-dispatch fixture did not modify the CI workflow text")
endif()
file(WRITE "${missing_manual_dispatch_workflow_path}" "${missing_manual_dispatch_workflow_text}")
run_ci_workflow_validation(
    "missing manual dispatch"
    "${missing_manual_dispatch_workflow_path}"
    FALSE
    "CI workflow is missing expected text 'workflow_dispatch:'"
)

set(missing_source_tree_validation_workflow_path "${work_dir}/missing-source-tree-validation-ci.yml")
set(missing_source_tree_validation_workflow_text "${workflow_text}")
string(REPLACE
    "      - name: Validate release source tree\n        shell: cmd\n        run: |\n          cmake -DSOURCE_DIR=. -P cmake/AssertReleaseSourceTree.cmake > release-source-tree-audit.txt 2>&1\n          set \"SOURCE_TREE_AUDIT_RESULT=%errorlevel%\"\n          type release-source-tree-audit.txt\n          if not \"%SOURCE_TREE_AUDIT_RESULT%\"==\"0\" exit /b %SOURCE_TREE_AUDIT_RESULT%\n          echo release source tree audit: passed\n          >> release-source-tree-audit.txt echo release source tree audit: passed\n\n"
    ""
    missing_source_tree_validation_workflow_text
    "${missing_source_tree_validation_workflow_text}"
)
if(missing_source_tree_validation_workflow_text STREQUAL workflow_text)
    message(FATAL_ERROR "missing-source-tree-validation fixture did not modify the CI workflow text")
endif()
file(WRITE "${missing_source_tree_validation_workflow_path}" "${missing_source_tree_validation_workflow_text}")
run_ci_workflow_validation(
    "missing source-tree validation"
    "${missing_source_tree_validation_workflow_path}"
    FALSE
    "CI workflow is missing expected text"
    "Validate release source tree"
)

set(missing_source_tree_evidence_workflow_path "${work_dir}/missing-source-tree-evidence-ci.yml")
set(missing_source_tree_evidence_workflow_text "${workflow_text}")
string(REPLACE
    "          if not exist release-source-tree-audit.txt (\n            echo release source tree audit log was not found.\n            exit /b 1\n          )\n          >> \"%EVIDENCE%\" echo [release source tree audit]\n          type release-source-tree-audit.txt >> \"%EVIDENCE%\" 2>&1\n          if errorlevel 1 exit /b %errorlevel%\n          >> \"%EVIDENCE%\" echo.\n"
    ""
    missing_source_tree_evidence_workflow_text
    "${missing_source_tree_evidence_workflow_text}"
)
if(missing_source_tree_evidence_workflow_text STREQUAL workflow_text)
    message(FATAL_ERROR "missing-source-tree-evidence fixture did not modify the CI workflow text")
endif()
file(WRITE "${missing_source_tree_evidence_workflow_path}" "${missing_source_tree_evidence_workflow_text}")
run_ci_workflow_validation(
    "missing source-tree evidence"
    "${missing_source_tree_evidence_workflow_path}"
    FALSE
    "CI workflow is missing expected text"
    "if not exist"
)

set(source_tree_validation_after_configure_workflow_path "${work_dir}/source-tree-validation-after-configure-ci.yml")
set(source_tree_validation_after_configure_workflow_text "${workflow_text}")
string(REPLACE
    "      - name: Validate release source tree"
    "      - name: __VALIDATE_RELEASE_SOURCE_TREE_PLACEHOLDER__"
    source_tree_validation_after_configure_workflow_text
    "${source_tree_validation_after_configure_workflow_text}"
)
string(REPLACE
    "      - name: Configure, build, and test"
    "      - name: Validate release source tree"
    source_tree_validation_after_configure_workflow_text
    "${source_tree_validation_after_configure_workflow_text}"
)
string(REPLACE
    "      - name: __VALIDATE_RELEASE_SOURCE_TREE_PLACEHOLDER__"
    "      - name: Configure, build, and test"
    source_tree_validation_after_configure_workflow_text
    "${source_tree_validation_after_configure_workflow_text}"
)
if(source_tree_validation_after_configure_workflow_text STREQUAL workflow_text)
    message(FATAL_ERROR "source-tree-validation-after-configure fixture did not modify the CI workflow text")
endif()
file(WRITE "${source_tree_validation_after_configure_workflow_path}" "${source_tree_validation_after_configure_workflow_text}")
run_ci_workflow_validation(
    "source-tree validation after configure"
    "${source_tree_validation_after_configure_workflow_path}"
    FALSE
    "CI workflow order violation"
    "release source tree audit"
    "configure/build/test"
)

set(push_only_attestation_workflow_path "${work_dir}/push-only-attestation-ci.yml")
set(push_only_attestation_workflow_text "${workflow_text}")
string(REPLACE
    "github.event.repository.private == false && (github.event_name == 'push' || github.event_name == 'workflow_dispatch')"
    "github.event_name == 'push' && github.event.repository.private == false"
    push_only_attestation_workflow_text
    "${push_only_attestation_workflow_text}"
)
if(push_only_attestation_workflow_text STREQUAL workflow_text)
    message(FATAL_ERROR "push-only-attestation fixture did not modify the CI workflow text")
endif()
file(WRITE "${push_only_attestation_workflow_path}" "${push_only_attestation_workflow_text}")
run_ci_workflow_validation(
    "push-only attestation"
    "${push_only_attestation_workflow_path}"
    FALSE
    "CI workflow is missing expected text"
    "workflow_dispatch"
)

set(missing_retention_workflow_path "${work_dir}/missing-retention-ci.yml")
set(missing_retention_workflow_text "${workflow_text}")
string(REPLACE
    "          retention-days: 14\n"
    ""
    missing_retention_workflow_text
    "${missing_retention_workflow_text}"
)
if(missing_retention_workflow_text STREQUAL workflow_text)
    message(FATAL_ERROR "missing-retention fixture did not modify the CI workflow text")
endif()
file(WRITE "${missing_retention_workflow_path}" "${missing_retention_workflow_text}")
run_ci_workflow_validation(
    "missing retention"
    "${missing_retention_workflow_path}"
    FALSE
    "CI workflow is missing expected text 'retention-days: 14'"
)

set(noncanonical_checksum_workflow_path "${work_dir}/noncanonical-checksum-ci.yml")
set(noncanonical_checksum_workflow_text "${workflow_text}")
string(REPLACE
    "          $expectedText = \"$expectedLine`n\"\n"
    ""
    noncanonical_checksum_workflow_text
    "${noncanonical_checksum_workflow_text}"
)
if(noncanonical_checksum_workflow_text STREQUAL workflow_text)
    message(FATAL_ERROR "noncanonical-checksum fixture did not modify the CI workflow text")
endif()
file(WRITE "${noncanonical_checksum_workflow_path}" "${noncanonical_checksum_workflow_text}")
run_ci_workflow_validation(
    "noncanonical checksum"
    "${noncanonical_checksum_workflow_path}"
    FALSE
    "CI workflow is missing expected text"
    "$expectedText ="
)

set(missing_checksum_pair_binding_workflow_path "${work_dir}/missing-checksum-pair-binding-ci.yml")
set(missing_checksum_pair_binding_workflow_text "${workflow_text}")
string(REPLACE
    "          $expectedChecksumName = \"$($zip.Name).sha256\"\n          if ($checksum.Name -cne $expectedChecksumName) {\n            throw \"expected release ZIP checksum sidecar to be named $expectedChecksumName, found $($checksum.Name)\"\n          }\n\n"
    ""
    missing_checksum_pair_binding_workflow_text
    "${missing_checksum_pair_binding_workflow_text}"
)
if(missing_checksum_pair_binding_workflow_text STREQUAL workflow_text)
    message(FATAL_ERROR "missing-checksum-pair-binding fixture did not modify the CI workflow text")
endif()
file(WRITE "${missing_checksum_pair_binding_workflow_path}" "${missing_checksum_pair_binding_workflow_text}")
run_ci_workflow_validation(
    "missing checksum pair binding"
    "${missing_checksum_pair_binding_workflow_path}"
    FALSE
    "CI workflow is missing expected text"
    "checksum sidecar"
)

set(missing_shared_checksum_validation_workflow_path "${work_dir}/missing-shared-checksum-validation-ci.yml")
set(missing_shared_checksum_validation_workflow_text "${workflow_text}")
string(REPLACE
    "          & cmake `\n            \"-DPACKAGE_PATH=$($zip.FullName)\" `\n            \"-DCHECKSUM_PATH=$($checksum.FullName)\" `\n            -P cmake/AssertPackageChecksum.cmake\n          if ($LASTEXITCODE -ne 0) {\n            throw \"release ZIP checksum validation failed with exit code $LASTEXITCODE\"\n          }\n\n"
    ""
    missing_shared_checksum_validation_workflow_text
    "${missing_shared_checksum_validation_workflow_text}"
)
if(missing_shared_checksum_validation_workflow_text STREQUAL workflow_text)
    message(FATAL_ERROR "missing-shared-checksum-validation fixture did not modify the CI workflow text")
endif()
file(WRITE "${missing_shared_checksum_validation_workflow_path}" "${missing_shared_checksum_validation_workflow_text}")
run_ci_workflow_validation(
    "missing shared checksum validation"
    "${missing_shared_checksum_validation_workflow_path}"
    FALSE
    "CI workflow is missing expected text"
    "release ZIP checksum"
)

set(missing_downloaded_shared_checksum_validation_workflow_path "${work_dir}/missing-downloaded-shared-checksum-validation-ci.yml")
set(missing_downloaded_shared_checksum_validation_workflow_text "${workflow_text}")
string(REPLACE
    "          & cmake `\n            \"-DPACKAGE_PATH=$($zip.FullName)\" `\n            \"-DCHECKSUM_PATH=$($checksum.FullName)\" `\n            -P cmake/AssertPackageChecksum.cmake\n          if ($LASTEXITCODE -ne 0) {\n            throw \"downloaded release ZIP checksum validation failed with exit code $LASTEXITCODE\"\n          }\n\n"
    ""
    missing_downloaded_shared_checksum_validation_workflow_text
    "${missing_downloaded_shared_checksum_validation_workflow_text}"
)
if(missing_downloaded_shared_checksum_validation_workflow_text STREQUAL workflow_text)
    message(FATAL_ERROR "missing-downloaded-shared-checksum-validation fixture did not modify the CI workflow text")
endif()
file(WRITE "${missing_downloaded_shared_checksum_validation_workflow_path}" "${missing_downloaded_shared_checksum_validation_workflow_text}")
run_ci_workflow_validation(
    "missing downloaded shared checksum validation"
    "${missing_downloaded_shared_checksum_validation_workflow_path}"
    FALSE
    "CI workflow is missing expected text"
    "downloaded release ZIP"
)

set(missing_expected_commit_workflow_path "${work_dir}/missing-expected-commit-ci.yml")
set(missing_expected_commit_workflow_text "${workflow_text}")
string(REPLACE
    "            \"-DEXPECTED_COMMIT_SHA=$env:GITHUB_SHA\" `\n"
    ""
    missing_expected_commit_workflow_text
    "${missing_expected_commit_workflow_text}"
)
if(missing_expected_commit_workflow_text STREQUAL workflow_text)
    message(FATAL_ERROR "missing-expected-commit fixture did not modify the CI workflow text")
endif()
file(WRITE "${missing_expected_commit_workflow_path}" "${missing_expected_commit_workflow_text}")
run_ci_workflow_validation(
    "missing expected commit"
    "${missing_expected_commit_workflow_path}"
    FALSE
    "CI workflow is missing expected text"
    "-DEXPECTED_COMMIT_SHA=$env:GITHUB_SHA"
)

set(missing_downloaded_evidence_validation_workflow_path "${work_dir}/missing-downloaded-evidence-validation-ci.yml")
set(missing_downloaded_evidence_validation_workflow_text "${workflow_text}")
string(REPLACE
    "          \"validated downloaded release evidence: $($evidenceFiles[0].FullName)\"\n"
    ""
    missing_downloaded_evidence_validation_workflow_text
    "${missing_downloaded_evidence_validation_workflow_text}"
)
if(missing_downloaded_evidence_validation_workflow_text STREQUAL workflow_text)
    message(FATAL_ERROR "missing-downloaded-evidence-validation fixture did not modify the CI workflow text")
endif()
file(WRITE "${missing_downloaded_evidence_validation_workflow_path}" "${missing_downloaded_evidence_validation_workflow_text}")
run_ci_workflow_validation(
    "missing downloaded evidence validation"
    "${missing_downloaded_evidence_validation_workflow_path}"
    FALSE
    "CI workflow is missing expected text"
    "validated downloaded release"
)

set(attestation_before_validation_workflow_path "${work_dir}/attestation-before-validation-ci.yml")
set(attestation_before_validation_workflow_text "${workflow_text}")
string(REPLACE
    "      - name: Validate downloaded release artifact"
    "      - name: __VALIDATE_DOWNLOADED_RELEASE_ARTIFACT_PLACEHOLDER__"
    attestation_before_validation_workflow_text
    "${attestation_before_validation_workflow_text}"
)
string(REPLACE
    "      - name: Generate release provenance attestation"
    "      - name: Validate downloaded release artifact"
    attestation_before_validation_workflow_text
    "${attestation_before_validation_workflow_text}"
)
string(REPLACE
    "      - name: __VALIDATE_DOWNLOADED_RELEASE_ARTIFACT_PLACEHOLDER__"
    "      - name: Generate release provenance attestation"
    attestation_before_validation_workflow_text
    "${attestation_before_validation_workflow_text}"
)
if(attestation_before_validation_workflow_text STREQUAL workflow_text)
    message(FATAL_ERROR "attestation-before-validation fixture did not modify the CI workflow text")
endif()
file(WRITE "${attestation_before_validation_workflow_path}" "${attestation_before_validation_workflow_text}")
run_ci_workflow_validation(
    "attestation before validation"
    "${attestation_before_validation_workflow_path}"
    FALSE
    "CI workflow order violation"
    "downloaded release artifact"
    "provenance attestation generation"
)
