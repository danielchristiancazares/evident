if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" test_suffix)
set(work_dir "${BUILD_DIR}/clang-tidy-bare-booleans-${test_suffix}")
set(good_dir "${work_dir}/good")
set(bad_dir "${work_dir}/bad")
file(MAKE_DIRECTORY "${good_dir}/src")
file(MAKE_DIRECTORY "${bad_dir}/src")

# The good fixture exercises the surfaces-only exemptions: a domain decision is modeled with a
# scoped enum, while an anonymous-namespace predicate helper and a function-local bool are
# internal-linkage implementation details that MUST be accepted.
file(WRITE "${good_dir}/src/good.cpp"
    "enum class Decision { Continue, Stop };\n"
    "Decision inspect(int value) {\n"
    "    return value == 0 ? Decision::Stop : Decision::Continue;\n"
    "}\n"
    "namespace {\n"
    "bool internal_predicate(int value) {\n"
    "    bool flag = value > 0;\n"
    "    return flag;\n"
    "}\n"
    "}\n"
)
file(WRITE "${good_dir}/compile_commands.json"
    "[{\"directory\":\"${good_dir}\",\"command\":\"clang++ -std=c++23 -c src/good.cpp\",\"file\":\"${good_dir}/src/good.cpp\"}]\n"
)

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${good_dir}"
        "-DBUILD_DIR=${good_dir}"
        -P "${CMAKE_CURRENT_LIST_DIR}/AssertClangTidyBareBooleans.cmake"
    RESULT_VARIABLE good_result
    OUTPUT_VARIABLE good_stdout
    ERROR_VARIABLE good_stderr
)

if(NOT good_result EQUAL 0)
    message(FATAL_ERROR
        "clang AST bare-bool rule rejected clean synthetic source\nstdout:\n${good_stdout}\nstderr:\n${good_stderr}"
    )
endif()

# The bad fixture puts bare bool on repo-defined surfaces: a type alias, a record field, an
# operator bool conversion, a namespace-scope variable, and a free function return and parameter.
file(WRITE "${bad_dir}/src/bad.cpp"
    "using Alias = bool;\n"
    "struct Bad {\n"
    "    bool field;\n"
    "    explicit operator bool() const { return field; }\n"
    "};\n"
    "bool surface_flag = false;\n"
    "bool returns_bool(bool parameter) {\n"
    "    return parameter;\n"
    "}\n"
)
file(WRITE "${bad_dir}/compile_commands.json"
    "[{\"directory\":\"${bad_dir}\",\"command\":\"clang++ -std=c++23 -c src/bad.cpp\",\"file\":\"${bad_dir}/src/bad.cpp\"}]\n"
)

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${bad_dir}"
        "-DBUILD_DIR=${bad_dir}"
        -P "${CMAKE_CURRENT_LIST_DIR}/AssertClangTidyBareBooleans.cmake"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_stdout
    ERROR_VARIABLE bad_stderr
)

if(bad_result EQUAL 0)
    message(FATAL_ERROR
        "clang AST bare-bool rule unexpectedly accepted synthetic violations\nstdout:\n${bad_stdout}\nstderr:\n${bad_stderr}"
    )
endif()

set(actual_error "${bad_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")

foreach(required_fragment IN ITEMS
    "C++ bare bool declarations are forbidden"
    "evident-bare-bool-return"
    "evident-bare-bool-parameter"
    "evident-bare-bool-field"
    "evident-bare-bool-variable"
    "evident-bare-bool-alias"
    "evident-bare-bool-conversion"
    "bad.cpp"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "clang AST bare-bool rejection did not contain required fragment '${required_fragment}'\nstdout:\n${bad_stdout}\nstderr:\n${bad_stderr}"
        )
    endif()
endforeach()

# Enforce the AST bare-bool rule against the real repository source tree.
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${SOURCE_DIR}"
        "-DBUILD_DIR=${BUILD_DIR}"
        -P "${CMAKE_CURRENT_LIST_DIR}/AssertClangTidyBareBooleans.cmake"
    RESULT_VARIABLE source_result
    OUTPUT_VARIABLE source_stdout
    ERROR_VARIABLE source_stderr
)

if(NOT source_result EQUAL 0)
    message(FATAL_ERROR
        "clang AST bare-bool rule rejected the repository source tree\nstdout:\n${source_stdout}\nstderr:\n${source_stderr}"
    )
endif()
