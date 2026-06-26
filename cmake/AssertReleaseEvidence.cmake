if(NOT DEFINED EVIDENCE_PATH)
    message(FATAL_ERROR "EVIDENCE_PATH is required")
endif()

if(NOT EXISTS "${EVIDENCE_PATH}")
    message(FATAL_ERROR "expected release evidence file does not exist: ${EVIDENCE_PATH}")
endif()

if(NOT DEFINED EXPECTED_CTEST_TOTAL)
    message(FATAL_ERROR "EXPECTED_CTEST_TOTAL is required")
endif()

if(NOT EXPECTED_CTEST_TOTAL MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "EXPECTED_CTEST_TOTAL must be a positive decimal test count: ${EXPECTED_CTEST_TOTAL}")
endif()

file(SIZE "${EVIDENCE_PATH}" evidence_size)
if(evidence_size LESS 1)
    message(FATAL_ERROR "expected release evidence file to be non-empty: ${EVIDENCE_PATH}")
endif()

set(package_detail_names
    RELEASE_PACKAGE_NAME
    RELEASE_PACKAGE_SIZE
    RELEASE_PACKAGE_SHA256
)

foreach(package_detail_name IN LISTS package_detail_names)
    if(NOT DEFINED ${package_detail_name})
        message(FATAL_ERROR
            "release package evidence details must provide RELEASE_PACKAGE_NAME, RELEASE_PACKAGE_SIZE, and RELEASE_PACKAGE_SHA256 together: ${EVIDENCE_PATH}")
    endif()
    if("${${package_detail_name}}" STREQUAL "")
        message(FATAL_ERROR "${package_detail_name} must not be empty")
    endif()
endforeach()

if(NOT RELEASE_PACKAGE_NAME MATCHES "^evident-[0-9][0-9]*\\.[0-9][0-9]*\\.[0-9][0-9]*-windows-x64\\.zip$")
    message(FATAL_ERROR
        "RELEASE_PACKAGE_NAME must match evident-<version>-windows-x64.zip: ${RELEASE_PACKAGE_NAME}")
endif()
string(REGEX REPLACE
    "^evident-([0-9][0-9]*\\.[0-9][0-9]*\\.[0-9][0-9]*)-windows-x64\\.zip$"
    "\\1"
    release_package_version
    "${RELEASE_PACKAGE_NAME}"
)
if(NOT RELEASE_PACKAGE_SIZE MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "RELEASE_PACKAGE_SIZE must be a positive decimal byte count: ${RELEASE_PACKAGE_SIZE}")
endif()
string(LENGTH "${RELEASE_PACKAGE_SHA256}" release_package_sha256_length)
if(NOT release_package_sha256_length EQUAL 64 OR NOT RELEASE_PACKAGE_SHA256 MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "RELEASE_PACKAGE_SHA256 must be a lowercase 64-character SHA256 hex digest: ${RELEASE_PACKAGE_SHA256}")
endif()

file(READ "${EVIDENCE_PATH}" evidence_text)
string(REPLACE "\r\n" "\n" evidence_text "${evidence_text}")
string(REPLACE "\r" "\n" evidence_text "${evidence_text}")
set(evidence_lines_for_search "\n${evidence_text}")

function(assert_line_marker_occurs_once description marker lines_variable)
    set(needle "\n${marker}")
    set(lines_for_search "${${lines_variable}}")
    set(search_offset 0)
    set(match_count 0)

    while(TRUE)
        string(SUBSTRING "${lines_for_search}" ${search_offset} -1 remaining_text)
        string(FIND "${remaining_text}" "${needle}" match_index)
        if(match_index EQUAL -1)
            break()
        endif()

        math(EXPR match_count "${match_count} + 1")
        math(EXPR search_offset "${search_offset} + ${match_index} + 1")
    endwhile()

    if(NOT match_count EQUAL 1)
        message(FATAL_ERROR
            "release evidence must contain exactly one ${description}; found ${match_count}: ${EVIDENCE_PATH}")
    endif()
endfunction()

function(assert_text_line_equals_once description expected_line text_variable)
    set(text_body "${${text_variable}}")
    string(REPLACE "\n" ";" text_lines "${text_body}")
    set(match_count 0)

    foreach(text_line IN LISTS text_lines)
        if(text_line STREQUAL "${expected_line}")
            math(EXPR match_count "${match_count} + 1")
        endif()
    endforeach()

    if(NOT match_count EQUAL 1)
        message(FATAL_ERROR
            "release evidence ${description} must contain exactly one expected line '${expected_line}'; found ${match_count}: ${EVIDENCE_PATH}")
    endif()
endfunction()

function(assert_text_line_prefix_absent description forbidden_prefix text_variable)
    set(text_body "${${text_variable}}")
    string(REPLACE "\n" ";" text_lines "${text_body}")
    set(match_count 0)

    foreach(text_line IN LISTS text_lines)
        string(FIND "${text_line}" "${forbidden_prefix}" prefix_index)
        if(prefix_index EQUAL 0)
            math(EXPR match_count "${match_count} + 1")
        endif()
    endforeach()

    if(match_count GREATER 0)
        message(FATAL_ERROR
            "release evidence ${description} must not contain lines starting with '${forbidden_prefix}'; found ${match_count}: ${EVIDENCE_PATH}")
    endif()
endfunction()

function(assert_text_line_prefix_has_value_once description line_prefix text_variable)
    set(text_body "${${text_variable}}")
    string(REPLACE "\n" ";" text_lines "${text_body}")
    set(match_count 0)
    string(LENGTH "${line_prefix}" line_prefix_length)

    foreach(text_line IN LISTS text_lines)
        string(FIND "${text_line}" "${line_prefix}" prefix_index)
        if(prefix_index EQUAL 0)
            math(EXPR match_count "${match_count} + 1")
            string(SUBSTRING "${text_line}" ${line_prefix_length} -1 line_value)
            string(STRIP "${line_value}" line_value)
            if(line_value STREQUAL "")
                message(FATAL_ERROR
                    "release evidence ${description} must contain a non-empty value after '${line_prefix}': ${EVIDENCE_PATH}")
            endif()
        endif()
    endforeach()

    if(NOT match_count EQUAL 1)
        message(FATAL_ERROR
            "release evidence ${description} must contain exactly one line starting with '${line_prefix}'; found ${match_count}: ${EVIDENCE_PATH}")
    endif()
endfunction()

function(assert_text_line_prefix_contains_once description line_prefix required_fragment text_variable)
    set(text_body "${${text_variable}}")
    string(REPLACE "\n" ";" text_lines "${text_body}")
    set(match_count 0)

    foreach(text_line IN LISTS text_lines)
        string(FIND "${text_line}" "${line_prefix}" prefix_index)
        if(prefix_index EQUAL 0)
            math(EXPR match_count "${match_count} + 1")
            string(FIND "${text_line}" "${required_fragment}" required_fragment_index)
            if(required_fragment_index EQUAL -1)
                message(FATAL_ERROR
                    "release evidence ${description} has line '${text_line}' missing required text '${required_fragment}': ${EVIDENCE_PATH}")
            endif()
        endif()
    endforeach()

    if(NOT match_count EQUAL 1)
        message(FATAL_ERROR
            "release evidence ${description} must contain exactly one line starting with '${line_prefix}'; found ${match_count}: ${EVIDENCE_PATH}")
    endif()
endfunction()

function(assert_text_line_matches_once description line_regex text_variable)
    set(text_body "${${text_variable}}")
    string(REPLACE "\n" ";" text_lines "${text_body}")
    set(match_count 0)

    foreach(text_line IN LISTS text_lines)
        if(text_line MATCHES "${line_regex}")
            math(EXPR match_count "${match_count} + 1")
        endif()
    endforeach()

    if(NOT match_count EQUAL 1)
        message(FATAL_ERROR
            "release evidence ${description} must contain exactly one line matching '${line_regex}'; found ${match_count}: ${EVIDENCE_PATH}")
    endif()
endfunction()

function(assert_text_line_prefix_value_matches_once description line_prefix value_regex text_variable)
    set(text_body "${${text_variable}}")
    string(REPLACE "\n" ";" text_lines "${text_body}")
    set(match_count 0)
    string(LENGTH "${line_prefix}" line_prefix_length)

    foreach(text_line IN LISTS text_lines)
        string(FIND "${text_line}" "${line_prefix}" prefix_index)
        if(prefix_index EQUAL 0)
            math(EXPR match_count "${match_count} + 1")
            string(SUBSTRING "${text_line}" ${line_prefix_length} -1 line_value)
            string(STRIP "${line_value}" line_value)
            if(line_value STREQUAL "")
                message(FATAL_ERROR
                    "release evidence ${description} must contain a non-empty value after '${line_prefix}': ${EVIDENCE_PATH}")
            endif()
            if(NOT line_value MATCHES "${value_regex}")
                message(FATAL_ERROR
                    "release evidence ${description} has invalid value '${line_value}' after '${line_prefix}': ${EVIDENCE_PATH}")
            endif()
        endif()
    endforeach()

    if(NOT match_count EQUAL 1)
        message(FATAL_ERROR
            "release evidence ${description} must contain exactly one line starting with '${line_prefix}'; found ${match_count}: ${EVIDENCE_PATH}")
    endif()
endfunction()

function(assert_compiler_toolchain_identity description text_variable)
    assert_text_line_equals_once(
        "${description} native target"
        "native target: x86_64-pc-windows-msvc"
        "${text_variable}"
    )
    assert_text_line_equals_once(
        "${description} supported native target"
        "supported native target: x86_64-pc-windows-msvc"
        "${text_variable}"
    )
    assert_text_line_prefix_value_matches_once(
        "${description} clang driver"
        "clang driver:"
        "(^|[\\\\/])clang(\\.exe)?$"
        "${text_variable}"
    )
    assert_text_line_equals_once(
        "${description} clang override environment variable"
        "clang override env: EVIDENT_CLANG"
        "${text_variable}"
    )
    assert_text_line_equals_once(
        "${description} linker mode"
        "linker mode: clang -fuse-ld=lld"
        "${text_variable}"
    )
endfunction()

function(assert_checked_compiler_toolchain description text_variable)
    assert_compiler_toolchain_identity("${description}" "${text_variable}")
    assert_text_line_prefix_value_matches_once(
        "${description} clang version probe"
        "clang version:"
        "^clang version .+"
        "${text_variable}"
    )
    assert_text_line_prefix_value_matches_once(
        "${description} lld-link version probe"
        "lld-link version:"
        "^LLD .+"
        "${text_variable}"
    )
endfunction()

function(assert_evidence_markers_in_order)
    set(previous_marker "")
    set(previous_marker_index -1)

    foreach(marker IN LISTS ARGN)
        string(FIND "${evidence_text}" "${marker}" marker_index)
        if(marker_index EQUAL -1)
            message(FATAL_ERROR "release evidence is missing expected ordered marker '${marker}': ${EVIDENCE_PATH}")
        endif()

        if(marker_index LESS previous_marker_index)
            message(FATAL_ERROR
                "release evidence sections must appear in canonical order; '${marker}' appears before '${previous_marker}': ${EVIDENCE_PATH}")
        endif()

        set(previous_marker "${marker}")
        set(previous_marker_index "${marker_index}")
    endforeach()
endfunction()

set(expected_evidence_section_headers
    "[release source tree audit]"
    "[validated commands]"
    "[ctest output]"
    "[vswhere]"
    "[where cmake]"
    "[cmake --version]"
    "[where ninja]"
    "[ninja --version]"
    "[where cl]"
    "[cl]"
    "[where clang]"
    "[clang --version]"
    "[where lld-link]"
    "[lld-link --version]"
    "[evidc --version]"
    "[evidc --print-toolchain]"
    "[evidc --check-toolchain]"
    "[release ZIP]"
)

function(assert_no_unexpected_evidence_section_headers)
    string(REPLACE "\n" ";" evidence_lines "${evidence_text}")
    set(line_number 0)
    foreach(evidence_line IN LISTS evidence_lines)
        math(EXPR line_number "${line_number} + 1")
        string(LENGTH "${evidence_line}" evidence_line_length)
        if(evidence_line_length LESS 2)
            continue()
        endif()

        string(SUBSTRING "${evidence_line}" 0 1 first_character)
        math(EXPR last_character_index "${evidence_line_length} - 1")
        string(SUBSTRING "${evidence_line}" ${last_character_index} 1 last_character)
        if(first_character STREQUAL "[" AND last_character STREQUAL "]")
            if(NOT evidence_line IN_LIST expected_evidence_section_headers)
                message(FATAL_ERROR
                    "release evidence contains unexpected section header '${evidence_line}' on line ${line_number}: ${EVIDENCE_PATH}")
            endif()
        endif()
    endforeach()
endfunction()

function(extract_evidence_section_body section_header output_variable)
    string(FIND "${evidence_text}" "${section_header}" section_header_index)
    if(section_header_index EQUAL -1)
        message(FATAL_ERROR "release evidence is missing expected section '${section_header}': ${EVIDENCE_PATH}")
    endif()

    string(LENGTH "${section_header}" section_header_length)
    math(EXPR section_body_start "${section_header_index} + ${section_header_length}")
    string(SUBSTRING "${evidence_text}" ${section_body_start} -1 section_tail)
    string(FIND "${section_tail}" "\n[" next_section_index)
    if(next_section_index EQUAL -1)
        set(section_body "${section_tail}")
    else()
        string(SUBSTRING "${section_tail}" 0 ${next_section_index} section_body)
    endif()

    string(REGEX REPLACE "^\n" "" section_body "${section_body}")
    string(REGEX REPLACE "\n+$" "" section_body "${section_body}")
    set(${output_variable} "${section_body}" PARENT_SCOPE)
endfunction()

function(assert_evidence_section_has_non_empty_output section_header)
    extract_evidence_section_body("${section_header}" section_body)
    string(STRIP "${section_body}" stripped_section_body)
    if(stripped_section_body STREQUAL "")
        message(FATAL_ERROR
            "release evidence ${section_header} section must contain non-empty output: ${EVIDENCE_PATH}")
    endif()
endfunction()

set(required_fragments
    "Evident Windows x64 release evidence"
    "commit:"
    "runner: windows-2022"
    "runner image os:"
    "runner image version:"
    "native target: x86_64-pc-windows-msvc"
    "package preset: windows-x64-ninja-package-checksum"
    "[release source tree audit]"
    "release source tree audit: passed"
    "[validated commands]"
    "cmake --preset windows-x64-ninja"
    "cmake --build --preset windows-x64-ninja"
    "ctest --preset windows-x64-ninja"
    "cmake --build --preset windows-x64-ninja-package-checksum"
    "[ctest output]"
    "[vswhere]"
    "[where cmake]"
    "[cmake --version]"
    "[where ninja]"
    "[ninja --version]"
    "[where cl]"
    "[cl]"
    "[where clang]"
    "[clang --version]"
    "[where lld-link]"
    "[lld-link --version]"
    "[evidc --version]"
    "[evidc --print-toolchain]"
    "[evidc --check-toolchain]"
    "[release ZIP]"
)

foreach(required_fragment IN LISTS required_fragments)
    string(FIND "${evidence_text}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR "release evidence is missing expected text '${required_fragment}': ${EVIDENCE_PATH}")
    endif()
endforeach()

assert_no_unexpected_evidence_section_headers()

assert_evidence_markers_in_order(
    "[release source tree audit]"
    "[validated commands]"
    "[ctest output]"
    "[vswhere]"
    "[where cmake]"
    "[cmake --version]"
    "[where ninja]"
    "[ninja --version]"
    "[where cl]"
    "[cl]"
    "[where clang]"
    "[clang --version]"
    "[where lld-link]"
    "[lld-link --version]"
    "[evidc --version]"
    "[evidc --print-toolchain]"
    "[evidc --check-toolchain]"
    "[release ZIP]"
)

extract_evidence_section_body("[release source tree audit]" source_tree_audit_section)
assert_text_line_equals_once(
    "[release source tree audit] pass line"
    "release source tree audit: passed"
    source_tree_audit_section
)

extract_evidence_section_body("[validated commands]" validated_commands_section)
set(expected_validated_commands "cmake --preset windows-x64-ninja
cmake --build --preset windows-x64-ninja
ctest --preset windows-x64-ninja
cmake --build --preset windows-x64-ninja-package-checksum")
if(NOT validated_commands_section STREQUAL expected_validated_commands)
    message(FATAL_ERROR
        "release evidence [validated commands] section must list exactly the supported release commands: ${EVIDENCE_PATH}")
endif()

extract_evidence_section_body("[ctest output]" ctest_output_section)
set(expected_ctest_summary "100% tests passed, 0 tests failed out of ${EXPECTED_CTEST_TOTAL}")
string(FIND "${ctest_output_section}" "${expected_ctest_summary}" ctest_summary_index)
if(ctest_summary_index EQUAL -1)
    message(FATAL_ERROR
        "release evidence [ctest output] section is missing expected CTest pass summary '${expected_ctest_summary}': ${EVIDENCE_PATH}")
endif()

extract_evidence_section_body("[vswhere]" vswhere_section_body)
assert_text_line_prefix_value_matches_once(
    "[vswhere] Visual Studio installation path"
    "installationPath:"
    "^[A-Za-z]:[\\\\/].+"
    vswhere_section_body
)
assert_text_line_prefix_has_value_once(
    "[vswhere] Visual Studio installation version"
    "installationVersion:"
    vswhere_section_body
)

extract_evidence_section_body("[cl]" cl_section_body)
assert_text_line_prefix_contains_once(
    "[cl] MSVC compiler target architecture"
    "Microsoft (R) C/C++ Optimizing Compiler"
    " for x64"
    cl_section_body
)

foreach(non_empty_output_section IN ITEMS
    "[cmake --version]"
    "[ninja --version]"
    "[cl]"
    "[clang --version]"
    "[lld-link --version]"
    "[evidc --version]"
    "[evidc --print-toolchain]"
    "[evidc --check-toolchain]"
)
    assert_evidence_section_has_non_empty_output("${non_empty_output_section}")
endforeach()

extract_evidence_section_body("[cmake --version]" cmake_version_section_body)
assert_text_line_prefix_has_value_once(
    "[cmake --version] tool version"
    "cmake version "
    cmake_version_section_body
)

extract_evidence_section_body("[ninja --version]" ninja_version_section_body)
assert_text_line_matches_once(
    "[ninja --version] tool version"
    "^[0-9][0-9]*(\\.[0-9][0-9]*)+.*$"
    ninja_version_section_body
)

extract_evidence_section_body("[clang --version]" clang_version_section_body)
assert_text_line_prefix_has_value_once(
    "[clang --version] tool version"
    "clang version "
    clang_version_section_body
)

extract_evidence_section_body("[lld-link --version]" lld_link_version_section_body)
assert_text_line_prefix_has_value_once(
    "[lld-link --version] tool version"
    "LLD "
    lld_link_version_section_body
)

extract_evidence_section_body("[evidc --version]" evidc_version_section_body)
set(expected_evidc_version_output "evidc ${release_package_version}")
if(NOT evidc_version_section_body STREQUAL expected_evidc_version_output)
    message(FATAL_ERROR
        "release evidence [evidc --version] section must exactly match release package version '${expected_evidc_version_output}': ${EVIDENCE_PATH}")
endif()

extract_evidence_section_body("[evidc --print-toolchain]" evidc_print_toolchain_section_body)
assert_compiler_toolchain_identity(
    "[evidc --print-toolchain]"
    evidc_print_toolchain_section_body
)

extract_evidence_section_body("[evidc --check-toolchain]" evidc_check_toolchain_section_body)
assert_checked_compiler_toolchain(
    "[evidc --check-toolchain]"
    evidc_check_toolchain_section_body
)

string(FIND "${evidence_text}" "[validated commands]" validated_commands_index)
if(validated_commands_index EQUAL -1)
    message(FATAL_ERROR "release evidence is missing [validated commands] section: ${EVIDENCE_PATH}")
endif()
string(SUBSTRING "${evidence_text}" 0 ${validated_commands_index} evidence_prologue)
set(evidence_prologue_lines_for_search "\n${evidence_prologue}")

string(FIND "${evidence_text}" "[release ZIP]" release_zip_index)
if(release_zip_index EQUAL -1)
    message(FATAL_ERROR "release evidence is missing [release ZIP] section: ${EVIDENCE_PATH}")
endif()
string(SUBSTRING "${evidence_text}" 0 ${release_zip_index} evidence_before_release_zip)
string(SUBSTRING "${evidence_text}" ${release_zip_index} -1 release_zip_section)
set(release_zip_section_lines_for_search "\n${release_zip_section}")
extract_evidence_section_body("[release ZIP]" release_zip_section_body)

assert_line_marker_occurs_once(
    "evidence header"
    "Evident Windows x64 release evidence"
    evidence_lines_for_search
)
assert_line_marker_occurs_once("commit line" "commit:" evidence_lines_for_search)
assert_line_marker_occurs_once("runner line" "runner:" evidence_lines_for_search)
assert_line_marker_occurs_once("runner image OS line" "runner image os:" evidence_lines_for_search)
assert_line_marker_occurs_once("runner image version line" "runner image version:" evidence_lines_for_search)
assert_line_marker_occurs_once(
    "native target line in the top provenance block"
    "native target:"
    evidence_prologue_lines_for_search
)
assert_line_marker_occurs_once(
    "package preset line"
    "package preset:"
    evidence_lines_for_search
)
assert_text_line_equals_once(
    "top provenance runner line"
    "runner: windows-2022"
    evidence_prologue
)
assert_text_line_prefix_value_matches_once(
    "top provenance runner image OS line"
    "runner image os:"
    "^win[0-9]+$"
    evidence_prologue
)
assert_text_line_prefix_has_value_once(
    "top provenance runner image version line"
    "runner image version:"
    evidence_prologue
)
assert_text_line_equals_once(
    "top provenance native target line"
    "native target: x86_64-pc-windows-msvc"
    evidence_prologue
)
assert_text_line_equals_once(
    "top provenance package preset line"
    "package preset: windows-x64-ninja-package-checksum"
    evidence_prologue
)
assert_line_marker_occurs_once("[release source tree audit] section" "[release source tree audit]" evidence_lines_for_search)
assert_line_marker_occurs_once("[validated commands] section" "[validated commands]" evidence_lines_for_search)
assert_line_marker_occurs_once("[ctest output] section" "[ctest output]" evidence_lines_for_search)
assert_line_marker_occurs_once("[vswhere] section" "[vswhere]" evidence_lines_for_search)
assert_line_marker_occurs_once("[where cmake] section" "[where cmake]" evidence_lines_for_search)
assert_line_marker_occurs_once("[cmake --version] section" "[cmake --version]" evidence_lines_for_search)
assert_line_marker_occurs_once("[where ninja] section" "[where ninja]" evidence_lines_for_search)
assert_line_marker_occurs_once("[ninja --version] section" "[ninja --version]" evidence_lines_for_search)
assert_line_marker_occurs_once("[where cl] section" "[where cl]" evidence_lines_for_search)
assert_line_marker_occurs_once("[cl] section" "[cl]" evidence_lines_for_search)
assert_line_marker_occurs_once("[where clang] section" "[where clang]" evidence_lines_for_search)
assert_line_marker_occurs_once("[clang --version] section" "[clang --version]" evidence_lines_for_search)
assert_line_marker_occurs_once("[where lld-link] section" "[where lld-link]" evidence_lines_for_search)
assert_line_marker_occurs_once("[lld-link --version] section" "[lld-link --version]" evidence_lines_for_search)
assert_line_marker_occurs_once("[evidc --version] section" "[evidc --version]" evidence_lines_for_search)
assert_line_marker_occurs_once("[evidc --print-toolchain] section" "[evidc --print-toolchain]" evidence_lines_for_search)
assert_line_marker_occurs_once("[evidc --check-toolchain] section" "[evidc --check-toolchain]" evidence_lines_for_search)
assert_line_marker_occurs_once("[release ZIP] section" "[release ZIP]" evidence_lines_for_search)

assert_line_marker_occurs_once(
    "release ZIP file line"
    "file:"
    release_zip_section_lines_for_search
)
assert_line_marker_occurs_once(
    "release ZIP byte-size line"
    "bytes:"
    release_zip_section_lines_for_search
)
assert_line_marker_occurs_once(
    "release ZIP SHA256 line"
    "sha256:"
    release_zip_section_lines_for_search
)
assert_text_line_equals_once(
    "[release ZIP] package file line"
    "file: ${RELEASE_PACKAGE_NAME}"
    release_zip_section_body
)
assert_text_line_equals_once(
    "[release ZIP] package byte-size line"
    "bytes: ${RELEASE_PACKAGE_SIZE}"
    release_zip_section_body
)
assert_text_line_equals_once(
    "[release ZIP] package SHA256 line"
    "sha256: ${RELEASE_PACKAGE_SHA256}"
    release_zip_section_body
)
assert_text_line_prefix_absent(
    "before [release ZIP] package file lines"
    "file:"
    evidence_before_release_zip
)
assert_text_line_prefix_absent(
    "before [release ZIP] package byte-size lines"
    "bytes:"
    evidence_before_release_zip
)
assert_text_line_prefix_absent(
    "before [release ZIP] package SHA256 lines"
    "sha256:"
    evidence_before_release_zip
)

string(REGEX MATCH "(^|\n)commit: ([^\n]+)" commit_match "${evidence_text}")
if(commit_match STREQUAL "")
    message(FATAL_ERROR "release evidence is missing a commit SHA line: ${EVIDENCE_PATH}")
endif()

set(commit_sha "${CMAKE_MATCH_2}")
string(LENGTH "${commit_sha}" commit_sha_length)
if(NOT commit_sha_length EQUAL 40 OR NOT commit_sha MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR
        "release evidence commit must be a lowercase 40-character Git SHA: ${commit_sha}")
endif()

if(DEFINED EXPECTED_COMMIT_SHA)
    if("${EXPECTED_COMMIT_SHA}" STREQUAL "")
        message(FATAL_ERROR "EXPECTED_COMMIT_SHA must not be empty")
    endif()

    string(LENGTH "${EXPECTED_COMMIT_SHA}" expected_commit_sha_length)
    if(NOT expected_commit_sha_length EQUAL 40 OR NOT EXPECTED_COMMIT_SHA MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR
            "EXPECTED_COMMIT_SHA must be a lowercase 40-character Git SHA: ${EXPECTED_COMMIT_SHA}")
    endif()

    if(NOT commit_sha STREQUAL EXPECTED_COMMIT_SHA)
        message(FATAL_ERROR
            "release evidence commit must match expected source commit ${EXPECTED_COMMIT_SHA}; found ${commit_sha}: ${EVIDENCE_PATH}")
    endif()
endif()

function(assert_evidence_matches description regex)
    string(REGEX MATCH "${regex}" evidence_match "${evidence_text}")
    if(evidence_match STREQUAL "")
        message(FATAL_ERROR "release evidence is missing expected ${description}: ${EVIDENCE_PATH}")
    endif()
endfunction()

function(assert_section_path_lines_are_absolute description section_header)
    extract_evidence_section_body("${section_header}" section_body)
    string(REPLACE "\n" ";" section_lines "${section_body}")
    set(path_line_count 0)
    foreach(section_line IN LISTS section_lines)
        if(section_line STREQUAL "")
            continue()
        endif()

        math(EXPR path_line_count "${path_line_count} + 1")
        if(NOT section_line MATCHES "^[A-Za-z]:[\\\\/].+")
            message(FATAL_ERROR
                "release evidence ${description} must contain only absolute Windows paths; found '${section_line}': ${EVIDENCE_PATH}")
        endif()
    endforeach()

    if(path_line_count LESS 1)
        message(FATAL_ERROR
            "release evidence ${description} must contain at least one absolute Windows path: ${EVIDENCE_PATH}")
    endif()
endfunction()

assert_evidence_matches(
    "absolute cmake path from [where cmake]"
    "\\[where cmake\\]\n[A-Za-z]:[\\\\/][^\n]+"
)
assert_evidence_matches(
    "absolute ninja path from [where ninja]"
    "\\[where ninja\\]\n[A-Za-z]:[\\\\/][^\n]+"
)
assert_evidence_matches(
    "absolute cl path from [where cl]"
    "\\[where cl\\]\n[A-Za-z]:[\\\\/][^\n]+"
)
assert_evidence_matches(
    "absolute clang path from [where clang]"
    "\\[where clang\\]\n[A-Za-z]:[\\\\/][^\n]+"
)
assert_evidence_matches(
    "absolute lld-link path from [where lld-link]"
    "\\[where lld-link\\]\n[A-Za-z]:[\\\\/][^\n]+"
)

assert_section_path_lines_are_absolute("[where cmake] section" "[where cmake]")
assert_section_path_lines_are_absolute("[where ninja] section" "[where ninja]")
assert_section_path_lines_are_absolute("[where cl] section" "[where cl]")
assert_section_path_lines_are_absolute("[where clang] section" "[where clang]")
assert_section_path_lines_are_absolute("[where lld-link] section" "[where lld-link]")
