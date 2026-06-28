if(NOT DEFINED WORKFLOW_PATH)
    message(FATAL_ERROR "WORKFLOW_PATH is required")
endif()

if(NOT EXISTS "${WORKFLOW_PATH}")
    message(FATAL_ERROR "expected CI workflow file does not exist: ${WORKFLOW_PATH}")
endif()

file(READ "${WORKFLOW_PATH}" workflow_text)
string(REPLACE "\r\n" "\n" workflow_text "${workflow_text}")
string(REPLACE "\r" "\n" workflow_text "${workflow_text}")

set(required_fragments
    "push:"
    "pull_request:"
    "workflow_dispatch:"
    "permissions:"
    "contents: read"
    "concurrency:"
    "cancel-in-progress: true"
    "runs-on: windows-2022"
    "timeout-minutes: 30"
    "timeout-minutes: 10"
    "EVIDENT_CLANG: clang"
    "Validate release source tree"
    "cmake -DSOURCE_DIR=. -P cmake/AssertReleaseSourceTree.cmake"
    "release-source-tree-audit.txt"
    "release source tree audit: passed"
    "if not exist release-source-tree-audit.txt"
    "release source tree audit log was not found."
    "[release source tree audit]"
    "type release-source-tree-audit.txt >> \"%EVIDENCE%\" 2>&1"
    "cmake --preset windows-x64-ninja"
    "cmake --build --preset windows-x64-ninja"
    "ctest --preset windows-x64-ninja"
    "cmake --build --preset windows-x64-ninja-package-checksum"
    "ctest-output.txt"
    "evident-release-evidence.txt"
    "commit: %GITHUB_SHA%"
    "cmake/AssertReleaseEvidence.cmake"
    "-DEXPECTED_CTEST_TOTAL=415"
    "actions/checkout@34e114876b0b11c390a56381ad16ebd13914f8d5"
    "persist-credentials: false"
    "actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02"
    "id: upload-release-zip"
    "name: evident-windows-x64-zip"
    "build/windows-x64-ninja/evident-*-windows-x64.zip"
    "build/windows-x64-ninja/evident-*-windows-x64.zip.sha256"
    "build/windows-x64-ninja/evident-release-evidence.txt"
    "expected release ZIP checksum sidecar to be named"
    "-DPACKAGE_PATH=$($zip.FullName)"
    "-DCHECKSUM_PATH=$($checksum.FullName)"
    "cmake/AssertPackageChecksum.cmake"
    "throw \"release ZIP checksum validation failed"
    "if-no-files-found: error"
    "retention-days: 14"
    "ARTIFACT_DIGEST:"
    "steps.upload-release-zip.outputs['artifact-digest']"
    "upload-artifact did not report an artifact digest"
    "^[0-9a-fA-F]{64}$"
    "release ZIP artifact digest:"
    "### Release Artifact"
    "attest-windows-x64-release:"
    "needs: windows-x64"
    "github.event.repository.private == false && (github.event_name == 'push' || github.event_name == 'workflow_dispatch')"
    "actions: read"
    "attestations: write"
    "id-token: write"
    "actions/download-artifact@634f93cb2916e3fdff6788551b99b062d0335ce0"
    "name: evident-windows-x64-zip"
    "path: release-artifact"
    "Validate downloaded release artifact"
    "actions/attest@281a49d4cbb0a72c9575a50d18f6deb515a11deb"
    "Generate release provenance attestation"
    "subject-path:"
    "release-artifact/evident-*-windows-x64.zip"
    "release-artifact/evident-*-windows-x64.zip.sha256"
    "release-artifact/evident-release-evidence.txt"
    "downloaded release ZIP checksum mismatch"
    "expected exactly one release ZIP"
    "expected exactly one release ZIP checksum"
    "expected exactly one release evidence file"
    "expected downloaded release ZIP checksum sidecar to be named"
    "throw \"downloaded release ZIP checksum validation failed"
    "$actualText = (Get-Content -Raw -Path $checksum.FullName).Replace(\"`r`n\", \"`n\").Replace(\"`r\", \"`n\")"
    "Get-FileHash -Algorithm SHA256 -Path $zip.FullName"
    "$expectedText = \"$expectedLine`n\""
    "if ($actualText -cne $expectedText)"
    "verified downloaded release ZIP checksum:"
    "-DEXPECTED_COMMIT_SHA=$env:GITHUB_SHA"
    "-DEVIDENCE_PATH=$($evidenceFiles[0].FullName)"
    "-DRELEASE_PACKAGE_NAME=$($zip.Name)"
    "-DRELEASE_PACKAGE_SIZE=$($zip.Length)"
    "-DRELEASE_PACKAGE_SHA256=$expectedHash"
    "downloaded release evidence validation failed"
    "validated downloaded release evidence:"
    "Verify release provenance attestations"
    "GH_TOKEN:"
    "github.token"
    "gh --version"
    "gh attestation verify"
    "--repo"
    "--signer-workflow"
    "--source-ref"
    "$repo/.github/workflows/ci.yml"
    "verified release provenance attestation for"
)

string(REGEX MATCHALL "(^|\n)[ ]*uses: [^ \n\r]+" action_references "${workflow_text}")
if(action_references STREQUAL "")
    message(FATAL_ERROR "CI workflow must reference pinned remote GitHub Actions: ${WORKFLOW_PATH}")
endif()

foreach(action_reference IN LISTS action_references)
    string(STRIP "${action_reference}" action_reference)
    string(REGEX REPLACE "^uses: " "" action_ref "${action_reference}")

    if(NOT action_ref MATCHES "@")
        message(FATAL_ERROR
            "CI workflow remote action reference must be pinned to a full 40-character commit SHA: ${action_reference}")
    endif()

    string(REGEX REPLACE "^.*@" "" action_ref "${action_ref}")
    string(LENGTH "${action_ref}" action_ref_length)
    if(NOT action_ref MATCHES "^[0-9a-f]+$" OR NOT action_ref_length EQUAL 40)
        message(FATAL_ERROR
            "CI workflow action reference must be pinned to a full 40-character commit SHA: ${action_reference}")
    endif()
endforeach()

string(REGEX MATCHALL "(^|\n)[ ]*permissions:" permission_headers "${workflow_text}")
list(LENGTH permission_headers permission_header_count)
if(NOT permission_header_count EQUAL 2)
    message(FATAL_ERROR
        "CI workflow must define exactly the top-level read-only permissions block and the attestation job permissions block; found ${permission_header_count}: ${WORKFLOW_PATH}")
endif()

set(expected_top_level_permissions_block "permissions:\n  contents: read\n")
string(REGEX MATCH "(^|\n)permissions:\n(  [^\n]+\n)*" top_level_permissions_block "${workflow_text}")
if(top_level_permissions_block STREQUAL "")
    message(FATAL_ERROR
        "CI workflow is missing the top-level GITHUB_TOKEN permissions block: ${WORKFLOW_PATH}")
endif()
string(REGEX REPLACE "^\n" "" top_level_permissions_block "${top_level_permissions_block}")
if(NOT top_level_permissions_block STREQUAL expected_top_level_permissions_block)
    message(FATAL_ERROR
        "CI workflow top-level GITHUB_TOKEN permissions must be exactly contents: read: ${WORKFLOW_PATH}")
endif()

set(expected_attestation_permissions_block "    permissions:\n      actions: read\n      attestations: write\n      contents: read\n      id-token: write\n")
string(FIND "${workflow_text}" "${expected_attestation_permissions_block}" attestation_permissions_index)
if(attestation_permissions_index EQUAL -1)
    message(FATAL_ERROR
        "CI workflow attestation job permissions must be exactly actions: read, attestations: write, contents: read, and id-token: write: ${WORKFLOW_PATH}")
endif()

string(REGEX MATCHALL "(^|\n)[ ]+[A-Za-z0-9-]+: write" write_permission_lines "${workflow_text}")
foreach(write_permission_line IN LISTS write_permission_lines)
    string(STRIP "${write_permission_line}" write_permission_line)
    if(NOT write_permission_line STREQUAL "attestations: write"
            AND NOT write_permission_line STREQUAL "id-token: write")
        message(FATAL_ERROR
            "CI workflow contains unexpected write permission '${write_permission_line}': ${WORKFLOW_PATH}")
    endif()
endforeach()

string(REGEX MATCHALL "uses: actions/checkout@[^ \n\r]+" checkout_references "${workflow_text}")
if(checkout_references STREQUAL "")
    message(FATAL_ERROR "CI workflow must use actions/checkout for source validation: ${WORKFLOW_PATH}")
endif()

string(REGEX MATCHALL
    "uses: actions/checkout@[^ \n\r]+\n[ ]+with:\n[ ]+persist-credentials: false"
    checkout_references_without_persisted_credentials
    "${workflow_text}"
)
list(LENGTH checkout_references checkout_count)
list(LENGTH checkout_references_without_persisted_credentials checkout_without_persisted_credentials_count)
if(NOT checkout_count EQUAL checkout_without_persisted_credentials_count)
    message(FATAL_ERROR
        "CI workflow checkout steps must set persist-credentials: false: ${WORKFLOW_PATH}")
endif()

foreach(required_fragment IN LISTS required_fragments)
    string(FIND "${workflow_text}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR "CI workflow is missing expected text '${required_fragment}': ${WORKFLOW_PATH}")
    endif()
endforeach()

function(assert_fragment_order description earlier_fragment later_fragment)
    string(FIND "${workflow_text}" "${earlier_fragment}" earlier_index)
    string(FIND "${workflow_text}" "${later_fragment}" later_index)

    if(earlier_index EQUAL -1)
        message(FATAL_ERROR "CI workflow order check is missing earlier fragment '${earlier_fragment}': ${WORKFLOW_PATH}")
    endif()
    if(later_index EQUAL -1)
        message(FATAL_ERROR "CI workflow order check is missing later fragment '${later_fragment}': ${WORKFLOW_PATH}")
    endif()
    if(NOT earlier_index LESS later_index)
        message(FATAL_ERROR
            "CI workflow order violation: ${description}; '${earlier_fragment}' must appear before '${later_fragment}': ${WORKFLOW_PATH}")
    endif()
endfunction()

assert_fragment_order(
    "release source tree audit must run before configure/build/test"
    "      - name: Validate release source tree"
    "      - name: Configure, build, and test"
)
assert_fragment_order(
    "release source tree audit evidence must be recorded before validated commands"
    "          >> \"%EVIDENCE%\" echo [release source tree audit]"
    "          >> \"%EVIDENCE%\" echo [validated commands]"
)
assert_fragment_order(
    "downloaded release artifact must be validated before provenance attestation generation"
    "      - name: Validate downloaded release artifact"
    "      - name: Generate release provenance attestation"
)
assert_fragment_order(
    "release provenance attestation must be generated before provenance verification"
    "      - name: Generate release provenance attestation"
    "      - name: Verify release provenance attestations"
)

foreach(forbidden_fragment IN ITEMS
    "actions/checkout@v4"
    "actions/upload-artifact@v4"
    "actions/download-artifact@v5"
    "actions/attest@v4"
    "steps.upload-release-zip.outputs.digest"
    "github.event_name == 'push' && github.event.repository.private == false"
    "$actualLine = $actualLine -replace"
    "cmake -DREQUIRE_CLEAN_WORKTREE=OFF"
    "windows-latest"
    "persist-credentials: true"
    "write-all"
)
    string(FIND "${workflow_text}" "${forbidden_fragment}" fragment_index)
    if(NOT fragment_index EQUAL -1)
        message(FATAL_ERROR "CI workflow contains forbidden text '${forbidden_fragment}': ${WORKFLOW_PATH}")
    endif()
endforeach()
