if(NOT DEFINED TESTS_DIR)
    message(FATAL_ERROR "TESTS_DIR is required")
endif()

if(NOT DEFINED CMAKE_LIST_PATH)
    message(FATAL_ERROR "CMAKE_LIST_PATH is required")
endif()

if(NOT EXISTS "${TESTS_DIR}")
    message(FATAL_ERROR "expected tests directory does not exist: ${TESTS_DIR}")
endif()

if(NOT EXISTS "${CMAKE_LIST_PATH}")
    message(FATAL_ERROR "expected CMakeLists file does not exist: ${CMAKE_LIST_PATH}")
endif()

file(READ "${CMAKE_LIST_PATH}" cmake_lists_text)

file(GLOB standalone_sources
    LIST_DIRECTORIES false
    RELATIVE "${TESTS_DIR}"
    "${TESTS_DIR}/*.evd"
)

file(GLOB expected_outputs
    LIST_DIRECTORIES false
    RELATIVE "${TESTS_DIR}"
    "${TESTS_DIR}/expected_*"
)

if(standalone_sources STREQUAL "")
    message(FATAL_ERROR "standalone source fixture corpus is empty: ${TESTS_DIR}")
endif()

if(expected_outputs STREQUAL "")
    message(FATAL_ERROR "expected-output corpus is empty: ${TESTS_DIR}")
endif()

function(assert_registered artifact_kind artifact_name)
    string(FIND "${cmake_lists_text}" "${artifact_name}" artifact_reference_index)
    if(artifact_reference_index EQUAL -1)
        message(FATAL_ERROR "${artifact_kind} is not registered in CMakeLists.txt: ${artifact_name}")
    endif()
endfunction()

foreach(standalone_source IN LISTS standalone_sources)
    assert_registered("standalone source fixture" "${standalone_source}")
endforeach()

foreach(expected_output IN LISTS expected_outputs)
    assert_registered("expected-output fixture" "${expected_output}")
endforeach()
