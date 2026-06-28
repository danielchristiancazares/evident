if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

find_program(CLANG_QUERY_EXECUTABLE NAMES clang-query REQUIRED)

set(compile_commands_path "${BUILD_DIR}/compile_commands.json")
if(NOT EXISTS "${compile_commands_path}")
    message(FATAL_ERROR "compile_commands.json is required at ${compile_commands_path}")
endif()

set(scan_roots)
foreach(candidate IN ITEMS "${SOURCE_DIR}/src")
    if(EXISTS "${candidate}")
        list(APPEND scan_roots "${candidate}")
    endif()
endforeach()

if(NOT scan_roots)
    message(FATAL_ERROR "no C++ source roots found under ${SOURCE_DIR}")
endif()

set(source_files)
foreach(scan_root IN LISTS scan_roots)
    foreach(extension IN ITEMS cpp cxx cc)
        file(GLOB_RECURSE matched_files
            LIST_DIRECTORIES FALSE
            "${scan_root}/*.${extension}"
        )
        list(APPEND source_files ${matched_files})
    endforeach()
endforeach()

list(REMOVE_DUPLICATES source_files)
list(SORT source_files)

# docs/CPP_DESIGN.md Section B forbids bare bool on repo-defined surfaces, not on
# internal-linkage implementation details. The following are implementation helpers and are
# exempt: declarations inside an anonymous namespace (internal linkage), function-local bool
# variables, and anything inside a lambda closure (call-operator return, captured fields,
# parameters). Bare bool on an externally reachable surface (free functions, parameters,
# record fields, namespace-scope variables, type aliases) and any `operator bool` conversion
# remain forbidden.
set(matchers
    "functionDecl(isExpansionInMainFile(), unless(hasAncestor(namespaceDecl(isAnonymous()))), unless(hasAncestor(cxxRecordDecl(isLambda()))), returns(booleanType())).bind(\"evident-bare-bool-return\")"
    "parmVarDecl(isExpansionInMainFile(), unless(hasAncestor(namespaceDecl(isAnonymous()))), unless(hasAncestor(cxxRecordDecl(isLambda()))), hasType(booleanType())).bind(\"evident-bare-bool-parameter\")"
    "fieldDecl(isExpansionInMainFile(), unless(hasAncestor(namespaceDecl(isAnonymous()))), unless(hasAncestor(cxxRecordDecl(isLambda()))), hasType(booleanType())).bind(\"evident-bare-bool-field\")"
    "varDecl(isExpansionInMainFile(), unless(parmVarDecl()), unless(hasAncestor(namespaceDecl(isAnonymous()))), hasGlobalStorage(), hasType(booleanType())).bind(\"evident-bare-bool-variable\")"
    "typeAliasDecl(isExpansionInMainFile(), unless(hasAncestor(namespaceDecl(isAnonymous()))), hasType(booleanType())).bind(\"evident-bare-bool-alias\")"
    "typedefDecl(isExpansionInMainFile(), unless(hasAncestor(namespaceDecl(isAnonymous()))), hasType(booleanType())).bind(\"evident-bare-bool-typedef\")"
    "cxxConversionDecl(isExpansionInMainFile(), returns(booleanType())).bind(\"evident-bare-bool-conversion\")"
)

set(violations)

foreach(source_file IN LISTS source_files)
    foreach(matcher IN LISTS matchers)
        execute_process(
            COMMAND "${CLANG_QUERY_EXECUTABLE}"
                "${source_file}"
                -p "${BUILD_DIR}"
                -c "set bind-root false"
                -c "match ${matcher}"
            RESULT_VARIABLE clang_query_result
            OUTPUT_VARIABLE clang_query_stdout
            ERROR_VARIABLE clang_query_stderr
        )
        if(NOT clang_query_result EQUAL 0)
            message(FATAL_ERROR
                "clang-query bare-bool rule failed for ${source_file}\nstdout:\n${clang_query_stdout}\nstderr:\n${clang_query_stderr}"
            )
        endif()
        string(FIND "${clang_query_stdout}" "0 matches." clean_match_index)
        if(clean_match_index EQUAL -1)
            string(SUBSTRING "${clang_query_stdout}" 0 2000 violation_excerpt)
            list(APPEND violations
                "${source_file}: forbidden bare bool by clang AST matcher:\n${violation_excerpt}"
            )
        endif()
    endforeach()
endforeach()

if(violations)
    string(REPLACE ";" "\n  " formatted_violations "${violations}")
    message(FATAL_ERROR
        "C++ bare bool declarations are forbidden by docs/CPP_DESIGN.md:\n  ${formatted_violations}"
    )
endif()
