if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" test_suffix)
set(work_dir "${BUILD_DIR}/package-zip-unsafe-entry-${test_suffix}")
set(package_root "evident-unsafe-entry-windows-x64")
set(package_path "${work_dir}/${package_root}.zip")
set(non_zip_package_path "${work_dir}/${package_root}.txt")
set(valid_entry "${package_root}/bin/evidc.exe")
file(MAKE_DIRECTORY "${work_dir}")

function(assert_unsafe_listing_rejected case_name unsafe_entry)
    set(listing_path "${work_dir}/${case_name}.listing.txt")
    file(WRITE "${listing_path}" "${valid_entry}\n${unsafe_entry}\n")

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DPACKAGE_PATH=${package_path}"
            "-DPACKAGE_LISTING_PATH=${listing_path}"
            "-DEXPECTED_ENTRIES=${valid_entry}"
            "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertPackageZipListing.cmake"
        RESULT_VARIABLE validation_result
        OUTPUT_VARIABLE validation_stdout
        ERROR_VARIABLE validation_stderr
    )

    if(validation_result EQUAL 0)
        message(FATAL_ERROR
            "${case_name} package ZIP listing validation unexpectedly accepted unsafe entry ${unsafe_entry}\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
    endif()

    set(actual_error "${validation_stderr}")
    string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
    string(REPLACE "\r" "\n" actual_error "${actual_error}")

    foreach(required_fragment IN LISTS ARGN)
        string(FIND "${actual_error}" "${required_fragment}" fragment_index)
        if(fragment_index EQUAL -1)
            message(FATAL_ERROR
                "${case_name} validation error did not contain required fragment '${required_fragment}'\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
        endif()
    endforeach()
endfunction()

function(assert_unsafe_expected_entry_rejected case_name unsafe_expected_entry)
    set(listing_path "${work_dir}/${case_name}.listing.txt")
    file(WRITE "${listing_path}" "${valid_entry}\n")

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DPACKAGE_PATH=${package_path}"
            "-DPACKAGE_LISTING_PATH=${listing_path}"
            "-DEXPECTED_ENTRIES=${unsafe_expected_entry}"
            "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertPackageZipListing.cmake"
        RESULT_VARIABLE validation_result
        OUTPUT_VARIABLE validation_stdout
        ERROR_VARIABLE validation_stderr
    )

    if(validation_result EQUAL 0)
        message(FATAL_ERROR
            "${case_name} package ZIP listing validation unexpectedly accepted unsafe expected entry ${unsafe_expected_entry}\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
    endif()

    set(actual_error "${validation_stderr}")
    string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
    string(REPLACE "\r" "\n" actual_error "${actual_error}")

    foreach(required_fragment IN LISTS ARGN)
        string(FIND "${actual_error}" "${required_fragment}" fragment_index)
        if(fragment_index EQUAL -1)
            message(FATAL_ERROR
                "${case_name} expected-entry validation error did not contain required fragment '${required_fragment}'\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
        endif()
    endforeach()
endfunction()

function(assert_duplicate_expected_entry_rejected)
    set(listing_path "${work_dir}/duplicate-expected.listing.txt")
    file(WRITE "${listing_path}" "${valid_entry}\n")
    set(duplicate_expected_entries "${valid_entry};${valid_entry}")

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DPACKAGE_PATH=${package_path}"
            "-DPACKAGE_LISTING_PATH=${listing_path}"
            "-DEXPECTED_ENTRIES=${duplicate_expected_entries}"
            "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertPackageZipListing.cmake"
        RESULT_VARIABLE validation_result
        OUTPUT_VARIABLE validation_stdout
        ERROR_VARIABLE validation_stderr
    )

    if(validation_result EQUAL 0)
        message(FATAL_ERROR
            "duplicate-expected package ZIP listing validation unexpectedly accepted duplicate expected entries\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
    endif()

    set(actual_error "${validation_stderr}")
    string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
    string(REPLACE "\r" "\n" actual_error "${actual_error}")

    foreach(required_fragment IN ITEMS
        "expected package entries contain a duplicate entry:"
        "${valid_entry}")
        string(FIND "${actual_error}" "${required_fragment}" fragment_index)
        if(fragment_index EQUAL -1)
            message(FATAL_ERROR
                "duplicate-expected validation error did not contain required fragment '${required_fragment}'\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
        endif()
    endforeach()
endfunction()

function(assert_duplicate_listing_rejected)
    set(listing_path "${work_dir}/duplicate.listing.txt")
    file(WRITE "${listing_path}" "${valid_entry}\n${valid_entry}\n")

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DPACKAGE_PATH=${package_path}"
            "-DPACKAGE_LISTING_PATH=${listing_path}"
            "-DEXPECTED_ENTRIES=${valid_entry}"
            "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertPackageZipListing.cmake"
        RESULT_VARIABLE validation_result
        OUTPUT_VARIABLE validation_stdout
        ERROR_VARIABLE validation_stderr
    )

    if(validation_result EQUAL 0)
        message(FATAL_ERROR
            "duplicate package ZIP listing validation unexpectedly accepted a duplicate entry\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
    endif()

    set(actual_error "${validation_stderr}")
    string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
    string(REPLACE "\r" "\n" actual_error "${actual_error}")

    foreach(required_fragment IN ITEMS
        "package ZIP contains duplicate entry:"
        "${valid_entry}")
        string(FIND "${actual_error}" "${required_fragment}" fragment_index)
        if(fragment_index EQUAL -1)
            message(FATAL_ERROR
                "duplicate validation error did not contain required fragment '${required_fragment}'\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
        endif()
    endforeach()
endfunction()

function(assert_non_zip_package_path_rejected)
    set(listing_path "${work_dir}/non-zip-package-path.listing.txt")
    file(WRITE "${listing_path}" "${valid_entry}\n")

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DPACKAGE_PATH=${non_zip_package_path}"
            "-DPACKAGE_LISTING_PATH=${listing_path}"
            "-DEXPECTED_ENTRIES=${valid_entry}"
            "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertPackageZipListing.cmake"
        RESULT_VARIABLE validation_result
        OUTPUT_VARIABLE validation_stdout
        ERROR_VARIABLE validation_stderr
    )

    if(validation_result EQUAL 0)
        message(FATAL_ERROR
            "non-zip package path validation unexpectedly accepted a non-ZIP package path\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
    endif()

    set(actual_error "${validation_stderr}")
    string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
    string(REPLACE "\r" "\n" actual_error "${actual_error}")

    foreach(required_fragment IN ITEMS
        "package path must name a .zip archive:"
        "${non_zip_package_path}")
        string(FIND "${actual_error}" "${required_fragment}" fragment_index)
        if(fragment_index EQUAL -1)
            message(FATAL_ERROR
                "non-zip package path validation error did not contain required fragment '${required_fragment}'\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
        endif()
    endforeach()
endfunction()

function(assert_bare_package_root_listing_rejected)
    set(listing_path "${work_dir}/bare-package-root.listing.txt")
    file(WRITE "${listing_path}" "${valid_entry}\n${package_root}\n")

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DPACKAGE_PATH=${package_path}"
            "-DPACKAGE_LISTING_PATH=${listing_path}"
            "-DEXPECTED_ENTRIES=${valid_entry}"
            "-P" "${CMAKE_CURRENT_LIST_DIR}/AssertPackageZipListing.cmake"
        RESULT_VARIABLE validation_result
        OUTPUT_VARIABLE validation_stdout
        ERROR_VARIABLE validation_stderr
    )

    if(validation_result EQUAL 0)
        message(FATAL_ERROR
            "bare-package-root package ZIP listing validation unexpectedly accepted a bare package root entry\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
    endif()

    set(actual_error "${validation_stderr}")
    string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
    string(REPLACE "\r" "\n" actual_error "${actual_error}")

    foreach(required_fragment IN ITEMS
        "package ZIP entry is a bare package root without a trailing slash:"
        "${package_root}")
        string(FIND "${actual_error}" "${required_fragment}" fragment_index)
        if(fragment_index EQUAL -1)
            message(FATAL_ERROR
                "bare-package-root validation error did not contain required fragment '${required_fragment}'\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
        endif()
    endforeach()
endfunction()

assert_non_zip_package_path_rejected()

assert_unsafe_listing_rejected(
    "backslash"
    "${package_root}\\bin\\evidc.exe"
    "package ZIP entry uses a backslash path separator:"
    "${package_root}\\bin\\evidc.exe"
)

assert_unsafe_listing_rejected(
    "absolute"
    "/${package_root}/bin/evidc.exe"
    "package ZIP entry uses an absolute path:"
    "/${package_root}/bin/evidc.exe"
)

assert_unsafe_listing_rejected(
    "drive-letter"
    "C:/${package_root}/bin/evidc.exe"
    "package ZIP entry uses a drive-letter absolute path:"
    "C:/${package_root}/bin/evidc.exe"
)

assert_unsafe_listing_rejected(
    "empty-component"
    "${package_root}//bin/evidc.exe"
    "package ZIP entry contains an empty path component:"
    "${package_root}//bin/evidc.exe"
)

assert_unsafe_listing_rejected(
    "dot-component"
    "${package_root}/./bin/evidc.exe"
    "package ZIP entry contains a '.' path component:"
    "${package_root}/./bin/evidc.exe"
)

assert_unsafe_listing_rejected(
    "dotdot-component"
    "${package_root}/../bin/evidc.exe"
    "package ZIP entry contains a '..' path component:"
    "${package_root}/../bin/evidc.exe"
)

assert_duplicate_listing_rejected()

assert_bare_package_root_listing_rejected()

assert_unsafe_expected_entry_rejected(
    "expected-backslash"
    "${package_root}\\bin\\evidc.exe"
    "expected package entry uses a backslash path separator:"
    "${package_root}\\bin\\evidc.exe"
)

assert_unsafe_expected_entry_rejected(
    "expected-outside-root"
    "other-root/bin/evidc.exe"
    "expected package entry is outside the expected package root"
    "other-root/bin/evidc.exe"
)

assert_unsafe_expected_entry_rejected(
    "expected-empty-component"
    "${package_root}//bin/evidc.exe"
    "expected package entry contains an empty path component:"
    "${package_root}//bin/evidc.exe"
)

assert_unsafe_expected_entry_rejected(
    "expected-directory"
    "${package_root}/bin/"
    "expected package entry must name a file, not a directory:"
    "${package_root}/bin/"
)

assert_duplicate_expected_entry_rejected()
