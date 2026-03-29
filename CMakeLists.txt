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

include(CTest)
if(BUILD_TESTING)
    add_test(NAME parse_valid_example
        COMMAND evidc --dump-hir ${CMAKE_CURRENT_SOURCE_DIR}/examples/feature_mode.evd)

    add_test(NAME dump_valid_example_mir
        COMMAND evidc --dump-mir ${CMAKE_CURRENT_SOURCE_DIR}/examples/feature_mode.evd)

    add_test(NAME parse_valid_yield_match
        COMMAND evidc ${CMAKE_CURRENT_SOURCE_DIR}/tests/valid_yield_match.evd)

    function(add_mir_compare_test name input_file expected_file)
        add_test(NAME ${name}
            COMMAND ${CMAKE_COMMAND}
                -DCOMMAND_PATH=$<TARGET_FILE:evidc>
                -DMODE=--dump-mir
                -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/tests/${input_file}
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

    add_mir_compare_test(compare_mir_plain_call valid_plain_call.evd expected_valid_plain_call.mir.txt)
    add_mir_compare_test(compare_mir_state_match valid_state_match.evd expected_valid_state_match.mir.txt)
    add_mir_compare_test(compare_mir_try valid_try.evd expected_valid_try.mir.txt)
    add_mir_compare_test(compare_mir_direct_fail valid_direct_fail.evd expected_valid_direct_fail.mir.txt)
    add_mir_compare_test(compare_mir_yield_match valid_yield_match.evd expected_valid_yield_match.mir.txt)
    add_mir_compare_test(compare_mir_permit_proof valid_permit_proof.evd expected_valid_permit_proof.mir.out)
    add_mir_compare_test(compare_mir_affine_proof_move valid_affine_proof_move.evd expected_valid_affine_proof_move.mir.out)
    add_hir_compare_test(compare_hir_affine_wrapper valid_affine_wrapper.evd expected_valid_affine_wrapper.hir.out)
    add_hir_compare_test(compare_hir_affine_generic_wrapper valid_affine_generic_wrapper.evd expected_valid_affine_generic_wrapper.hir.out)
    add_test(NAME parse_valid_copyable_generic_wrapper
        COMMAND evidc ${CMAKE_CURRENT_SOURCE_DIR}/tests/valid_copyable_generic_wrapper.evd)
    add_test(NAME parse_valid_affine_branch_diverge
        COMMAND evidc ${CMAKE_CURRENT_SOURCE_DIR}/tests/valid_affine_branch_diverge.evd)

    add_emit_compare_test(compare_llvm_plain_call --emit-llvm valid_plain_call.evd expected_valid_plain_call.llvm.txt compare_valid_plain_call.ll)
    add_emit_compare_test(compare_llvm_state_match --emit-llvm valid_state_match.evd expected_valid_state_match.llvm.txt compare_valid_state_match.ll)
    add_emit_compare_test(compare_llvm_try --emit-llvm valid_try.evd expected_valid_try.llvm.txt compare_valid_try.ll)
    add_emit_compare_test(compare_llvm_direct_fail --emit-llvm valid_direct_fail.evd expected_valid_direct_fail.llvm.txt compare_valid_direct_fail.ll)
    add_emit_compare_test(compare_llvm_yield_match --emit-llvm valid_yield_match.evd expected_valid_yield_match.llvm.txt compare_valid_yield_match.ll)

    add_emit_exists_test(emit_obj_plain_call --emit-obj valid_plain_call.evd valid_plain_call.obj)
    add_emit_exists_test(emit_obj_state_match --emit-obj valid_state_match.evd valid_state_match.obj)
    add_emit_exists_test(emit_obj_try --emit-obj valid_try.evd valid_try.obj)
    add_emit_exists_test(emit_obj_direct_fail --emit-obj valid_direct_fail.evd valid_direct_fail.obj)
    add_emit_exists_test(emit_obj_yield_match --emit-obj valid_yield_match.evd valid_yield_match.obj)
    add_emit_exists_test(emit_obj_permit_proof --emit-obj valid_permit_proof.evd valid_permit_proof.obj)
    add_emit_exists_test(emit_asm_constant --emit-asm native_main_constant.evd native_main_constant.s)
    add_emit_exists_test(emit_obj_constant --emit-obj native_main_constant.evd native_main_constant.obj)

    add_emit_pattern_test(hygiene_llvm_plain_call_insertvalue
        valid_plain_call.evd
        hygiene_valid_plain_call.ll
        "insertvalue %evid.type.Value zeroinitializer, i64 %load0, 0"
        "getelementptr inbounds %evid.type.Value")
    add_emit_pattern_test(hygiene_llvm_try_invoke_extractvalue
        valid_try.evd
        hygiene_valid_try.ll
        "extractvalue %evid.yield.open %invoke1, 0"
        "invoke.slot")
    add_emit_pattern_test(hygiene_llvm_direct_fail_text_value
        valid_direct_fail.evd
        hygiene_valid_direct_fail.ll
        "insertvalue %evid.yield.boot zeroinitializer, i8 1, 0"
        "alloca { ptr, i64 }")
    add_emit_pattern_test(hygiene_llvm_yield_match_text_value
        valid_yield_match.evd
        hygiene_valid_yield_match.ll
        "insertvalue { ptr, i64 } zeroinitializer, ptr %strptr"
        "%lit")
    add_emit_pattern_test(hygiene_llvm_permit_signature_erased
        valid_permit_proof.evd
        hygiene_valid_permit_proof_signature.ll
        "define i64 @evid$submit(%evid.type.Mesh %arg0)"
        "@evid$submit(%evid.type.RenderPassActive")
    add_emit_pattern_test(hygiene_llvm_permit_storage_erased
        valid_permit_proof.evd
        hygiene_valid_permit_proof_storage.ll
        "call i8 @evid$start_pass()"
        "%slot3 = alloca %evid.type.RenderPassActive")

    add_emit_exe_test(run_native_constant native_main_constant.evd 7 native_main_constant.exe)
    add_emit_exe_test(run_native_call_chain native_main_call_chain.evd 8 native_main_call_chain.exe)
    add_emit_exe_test(run_native_state_match native_main_state_match.evd 4 native_main_state_match.exe)
    add_emit_exe_test(run_native_try native_main_try.evd 11 native_main_try.exe)
    add_emit_exe_test(run_native_yield_match native_main_yield_match.evd 3 native_main_yield_match.exe)
    add_emit_exe_test(run_native_with_permit native_main_with_permit.evd 7 native_main_with_permit.exe)

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
    add_reject_test(reject_direct_proof_construct invalid_direct_proof_construct.evd)
    add_reject_test(reject_prove_without_authority invalid_prove_without_authority.evd)
    add_reject_test(reject_prove_wrong_proof invalid_prove_wrong_proof.evd)
    add_reject_test(reject_foreign_proves invalid_foreign_proves.evd)
    add_reject_test(reject_grant_direct_call invalid_grant_direct_call.evd)
    add_reject_test(reject_with_non_grant invalid_with_non_grant.evd)
    add_reject_test(reject_permit_escape_let invalid_permit_escape_let.evd)
    add_reject_test(reject_missing_permit_argument invalid_missing_permit_argument.evd)
    add_reject_test(reject_affine_reuse_argument invalid_affine_reuse_argument.evd)
    add_reject_test(reject_affine_after_let_move invalid_affine_after_let_move.evd)
    add_reject_test(reject_affine_after_wrapper_move invalid_affine_after_wrapper_move.evd)
    add_reject_test(reject_affine_generic_wrapper_move invalid_affine_generic_wrapper_move.evd)
    add_reject_test(reject_affine_after_match invalid_affine_after_match.evd)

    add_backend_reject_test(reject_backend_missing_main valid_plain_call.evd reject_backend_missing_main.exe)
    add_backend_reject_test(reject_backend_private_main native_invalid_main_private.evd reject_backend_private_main.exe)
    add_backend_reject_test(reject_backend_main_params native_invalid_main_params.evd reject_backend_main_params.exe)
    add_backend_reject_test(reject_backend_main_wrong_return native_invalid_main_wrong_return.evd reject_backend_main_wrong_return.exe)
    add_backend_reject_test(reject_backend_main_yields native_invalid_main_yields.evd reject_backend_main_yields.exe)
endif()
