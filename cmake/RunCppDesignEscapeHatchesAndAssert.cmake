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
    "#include <cstring>\n"
    "#include <exception>\n"
    "#include <expected>\n"
    "#include <condition_variable>\n"
    "#include <memory>\n"
    "#include <mutex>\n"
    "#include <optional>\n"
    "#include <typeinfo>\n"
    "#include <variant>\n"
    "#include <vector>\n"
    "std::expected<void, int> empty_success();\n"
    "std::optional<int> maybe_value;\n"
    "std::vector<bool> packed_flags();\n"
    "std::monostate empty_state();\n"
    "std::shared_mutex global_mutex;\n"
    "std::condition_variable global_condition;\n"
    "std::shared_ptr<std::mutex> shared_mutex_handle();\n"
    "std::weak_ptr<int> weak_absence;\n"
    "std::nullptr_t null_branch;\n"
    "class Token { friend class Forgery; public: operator bool() const; };\n"
    "void bad(auto* value) { (void)dynamic_cast<void*>(value); (void)typeid(value); std::terminate(); memset(value, 0, 1); }\n"
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
        "C++ design escape-hatch scan unexpectedly accepted a synthetic dynamic_cast violation\nstdout:\n${bad_stdout}\nstderr:\n${bad_stderr}"
    )
endif()

set(actual_error "${bad_stderr}")
string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
string(REPLACE "\r" "\n" actual_error "${actual_error}")

foreach(required_fragment IN ITEMS
    "C++ design escape hatches are forbidden"
    "dynamic_cast"
    "std::expected<void, E>"
    "std::optional"
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
    "C-style scalar cast"
    "typeid"
    "std::terminate"
    "memset"
    "bad.cpp"
)
    string(FIND "${actual_error}" "${required_fragment}" fragment_index)
    if(fragment_index EQUAL -1)
        message(FATAL_ERROR
            "C++ design escape-hatch rejection did not contain required fragment '${required_fragment}'\nstdout:\n${bad_stdout}\nstderr:\n${bad_stderr}"
        )
    endif()
endforeach()
