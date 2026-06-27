if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${SOURCE_DIR}"
        -P "${CMAKE_CURRENT_LIST_DIR}/AssertCppDesignEscapeHatches.cmake"
    RESULT_VARIABLE clean_result
    OUTPUT_VARIABLE clean_stdout
    ERROR_VARIABLE clean_stderr
)

if(NOT clean_result EQUAL 0)
    message(FATAL_ERROR
        "C++ design escape-hatch scan failed for source tree with exit code ${clean_result}\nstdout:\n${clean_stdout}\nstderr:\n${clean_stderr}"
    )
endif()

string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" test_suffix)
set(work_dir "${BUILD_DIR}/cpp-design-escape-hatches-${test_suffix}")
set(synthetic_src "${work_dir}/src")
file(MAKE_DIRECTORY "${synthetic_src}")
file(WRITE "${synthetic_src}/bad.cpp"
    "#include <any>\n"
    "#include <cassert>\n"
    "#include <cstring>\n"
    "#include <cstdint>\n"
    "#include <cstdlib>\n"
    "#include <exception>\n"
    "#include <expected>\n"
    "#include <condition_variable>\n"
    "#include <memory>\n"
    "#include <mutex>\n"
    "#include <optional>\n"
    "#include <typeinfo>\n"
    "#include <variant>\n"
    "#include <vector>\n"
    "struct Missing;\n"
    "struct NotFound;\n"
    "std::expected<void, int> empty_success();\n"
    "std::expected<int, Missing> maybe_missing();\n"
    "std::expected<int, NotFound> maybe_not_found();\n"
    "std::optional<int> maybe_value;\n"
    "bool ambient_flag();\n"
    "std::vector<bool> packed_flags();\n"
    "std::any erased_state;\n"
    "std::monostate empty_state();\n"
    "std::shared_mutex global_mutex;\n"
    "std::condition_variable global_condition;\n"
    "std::shared_ptr<std::mutex> shared_mutex_handle();\n"
    "std::weak_ptr<int> weak_absence;\n"
    "std::nullptr_t null_branch;\n"
    "std::expected<int, int> maybe_number();\n"
    "class Token { friend class Forgery; public: operator bool() const; };\n"
    "void bad(auto* value, char const* text) {\n"
    "    assert(value != nullptr);\n"
    "    (void)dynamic_cast<void*>(value);\n"
    "    (void)typeid(value);\n"
    "    (void)const_cast<char*>(text);\n"
    "    (void)reinterpret_cast<std::uintptr_t>(value);\n"
    "    (void)(char*)value;\n"
    "    std::terminate();\n"
    "    memset(value, 0, 1);\n"
    "    std::memset(value, 0, 1);\n"
    "    calloc(1, 1);\n"
    "    std::calloc(1, 1);\n"
    "}\n"
    "int bad_expected() { return maybe_number().value() + maybe_number().value_or(0); }\n"
    "int bad_scalar_cast(auto value) { return (int)value; }\n"
)

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${work_dir}"
        -P "${CMAKE_CURRENT_LIST_DIR}/AssertCppDesignEscapeHatches.cmake"
    RESULT_VARIABLE bad_result
    OUTPUT_VARIABLE bad_stdout
    ERROR_VARIABLE bad_stderr
)

if(bad_result EQUAL 0)
    message(FATAL_ERROR
        "C++ design escape-hatch scan unexpectedly accepted synthetic escape-hatch violations\nstdout:\n${bad_stdout}\nstderr:\n${bad_stderr}"
    )
endif()

set(actual_error "${bad_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")

foreach(required_fragment IN ITEMS
    "C++ design escape hatches are forbidden"
    "dynamic_cast"
    "const_cast"
    "reinterpret_cast"
    "std::expected<void, E>"
    "std::expected<T, Missing>"
    "std::expected<T, NotFound>"
    ".value()"
    ".value_or()"
    "assert()"
    "std::optional"
    "std::any"
    "bare bool"
    "std::vector<bool>"
    "std::monostate"
    "std::shared_mutex"
    "std::condition_variable"
    "std::shared_ptr<std::mutex>"
    "std::weak_ptr"
    "std::nullptr_t"
    "friend class"
    "operator bool"
    "void*"
    "C-style void cast"
    "C-style pointer cast"
    "C-style scalar cast"
    "typeid"
    "std::terminate"
    "memset"
    "std::memset"
    "calloc"
    "std::calloc"
    "bad.cpp"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "C++ design escape-hatch rejection did not contain required fragment '${required_fragment}'\nstdout:\n${bad_stdout}\nstderr:\n${bad_stderr}"
        )
    endif()
endforeach()
