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

file(GLOB invalid_sources
    RELATIVE "${TESTS_DIR}"
    "${TESTS_DIR}/invalid_*.evd"
    "${TESTS_DIR}/native_invalid_*.evd"
)

file(GLOB diagnostic_goldens
    RELATIVE "${TESTS_DIR}"
    "${TESTS_DIR}/expected_*.diag.out"
)

if(invalid_sources STREQUAL "")
    message(FATAL_ERROR "invalid source corpus is empty: ${TESTS_DIR}")
endif()

if(diagnostic_goldens STREQUAL "")
    message(FATAL_ERROR "diagnostic golden corpus is empty: ${TESTS_DIR}")
endif()

foreach(invalid_source IN LISTS invalid_sources)
    string(FIND "${cmake_lists_text}" "${invalid_source}" source_reference_index)
    if(source_reference_index EQUAL -1)
        message(FATAL_ERROR "invalid source fixture is not registered in CMakeLists.txt: ${invalid_source}")
    endif()
endforeach()

foreach(diagnostic_golden IN LISTS diagnostic_goldens)
    string(FIND "${cmake_lists_text}" "${diagnostic_golden}" golden_reference_index)
    if(golden_reference_index EQUAL -1)
        message(FATAL_ERROR "diagnostic golden is not registered in CMakeLists.txt: ${diagnostic_golden}")
    endif()
endforeach()
