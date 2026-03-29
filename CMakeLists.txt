cmake_minimum_required(VERSION 3.24)
project(evident_compiler LANGUAGES CXX)

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
    src/Semantic.cpp
    src/Hir.cpp
    src/Driver.cpp
)

target_include_directories(evident_frontend PUBLIC include)

target_compile_options(evident_frontend PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

add_executable(evidc src/main.cpp)
target_link_libraries(evidc PRIVATE evident_frontend)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME parse_valid_example
        COMMAND evidc --dump-hir ${CMAKE_CURRENT_SOURCE_DIR}/examples/feature_mode.evd)

    add_test(NAME parse_valid_yield_match
        COMMAND evidc ${CMAKE_CURRENT_SOURCE_DIR}/tests/valid_yield_match.evd)

    function(add_reject_test name file)
        add_test(NAME ${name}
            COMMAND evidc ${CMAKE_CURRENT_SOURCE_DIR}/tests/${file})
        set_tests_properties(${name} PROPERTIES WILL_FAIL TRUE)
    endfunction()

    add_reject_test(reject_reserved_public_names invalid_reserved_names.evd)
    add_reject_test(reject_public_private_leak invalid_private_type_leak.evd)
    add_reject_test(reject_non_reason_yields invalid_non_reason_yields.evd)
    add_reject_test(reject_duplicate_decl invalid_duplicate_decl.evd)
    add_reject_test(reject_empty_state invalid_empty_state.evd)
    add_reject_test(reject_pseudo_optional invalid_pseudo_optional.evd)
    add_reject_test(reject_foreign_body invalid_foreign_body.evd)
    add_reject_test(reject_foreign_yields invalid_foreign_yields.evd)
    add_reject_test(reject_unknown_type invalid_unknown_type.evd)
    add_reject_test(reject_non_exhaustive_match invalid_non_exhaustive_match.evd)
    add_reject_test(reject_unhandled_yield invalid_yielded_call_unhandled.evd)
    add_reject_test(reject_try_outside_yields invalid_try_outside_yields.evd)
    add_reject_test(reject_fail_wrong_reason invalid_fail_wrong_reason.evd)
    add_reject_test(reject_match_wildcard invalid_match_wildcard.evd)
endif()
