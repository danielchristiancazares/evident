if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

function(run_test_corpus_registration validation_name tests_dir cmake_list_path)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DTESTS_DIR=${tests_dir}"
            "-DCMAKE_LIST_PATH=${cmake_list_path}"
            "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertTestCorpusRegistered.cmake"
        RESULT_VARIABLE validation_result
        OUTPUT_VARIABLE validation_stdout
        ERROR_VARIABLE validation_stderr
    )

    if(validation_result EQUAL 0)
        message(FATAL_ERROR
            "${validation_name} unexpectedly accepted an unregistered test corpus artifact\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
    endif()

    set(actual_error "${validation_stderr}")
    string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
    string(REPLACE "\r" "\n" actual_error "${actual_error}")

    foreach(required_fragment IN LISTS ARGN)
        string(FIND "${actual_error}" "${required_fragment}" fragment_index)
        if(fragment_index EQUAL -1)
            message(FATAL_ERROR
                "${validation_name} error did not contain required fragment '${required_fragment}'\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
        endif()
    endforeach()
endfunction()

function(write_registered_fixture fixture_dir)
    file(MAKE_DIRECTORY "${fixture_dir}/tests")
    file(WRITE "${fixture_dir}/CMakeLists.txt" "registered.evd\nexpected_registered.stdout.out\n")
    file(WRITE "${fixture_dir}/tests/registered.evd" "")
    file(WRITE "${fixture_dir}/tests/expected_registered.stdout.out" "")
endfunction()

string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" test_suffix)
set(work_dir "${BUILD_DIR}/test-corpus-registration-${test_suffix}")

set(unregistered_source_dir "${work_dir}/unregistered-source")
write_registered_fixture("${unregistered_source_dir}")
file(WRITE "${unregistered_source_dir}/tests/orphan.evd" "")
run_test_corpus_registration(
    "unregistered source fixture"
    "${unregistered_source_dir}/tests"
    "${unregistered_source_dir}/CMakeLists.txt"
    "standalone source fixture is not registered in CMakeLists.txt"
    "orphan.evd"
)

set(unregistered_expected_dir "${work_dir}/unregistered-expected")
write_registered_fixture("${unregistered_expected_dir}")
file(WRITE "${unregistered_expected_dir}/tests/expected_orphan.stdout.out" "")
run_test_corpus_registration(
    "unregistered expected-output fixture"
    "${unregistered_expected_dir}/tests"
    "${unregistered_expected_dir}/CMakeLists.txt"
    "expected-output fixture is not registered in CMakeLists.txt"
    "expected_orphan.stdout.out"
)
