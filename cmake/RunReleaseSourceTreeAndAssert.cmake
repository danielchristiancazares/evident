if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

find_program(GIT_EXECUTABLE git)
if(NOT GIT_EXECUTABLE)
    message(FATAL_ERROR "git is required to validate the release source-tree contract")
endif()

function(run_git repo_path)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${repo_path}" ${ARGN}
        RESULT_VARIABLE git_result
        OUTPUT_VARIABLE git_stdout
        ERROR_VARIABLE git_stderr
    )
    if(NOT git_result EQUAL 0)
        message(FATAL_ERROR
            "git ${ARGN} failed in ${repo_path} with exit code ${git_result}\nstdout:\n${git_stdout}\nstderr:\n${git_stderr}")
    endif()
endfunction()

function(init_clean_repo repo_path)
    file(MAKE_DIRECTORY "${repo_path}")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" init -q "${repo_path}"
        RESULT_VARIABLE git_init_result
        OUTPUT_VARIABLE git_init_stdout
        ERROR_VARIABLE git_init_stderr
    )
    if(NOT git_init_result EQUAL 0)
        message(FATAL_ERROR
            "git init failed in ${repo_path} with exit code ${git_init_result}\nstdout:\n${git_init_stdout}\nstderr:\n${git_init_stderr}")
    endif()

    file(WRITE "${repo_path}/README.md" "synthetic release source tree\n")
    run_git("${repo_path}" add README.md)
    run_git("${repo_path}" -c user.name=Evident -c user.email=evident@example.invalid commit -q -m "initial source tree")
endfunction()

function(run_release_source_tree_validation validation_name source_dir should_pass)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE_DIR=${source_dir}"
            -P "${CMAKE_CURRENT_LIST_DIR}/AssertReleaseSourceTree.cmake"
        RESULT_VARIABLE validation_result
        OUTPUT_VARIABLE validation_stdout
        ERROR_VARIABLE validation_stderr
    )

    if(should_pass)
        if(NOT validation_result EQUAL 0)
            message(FATAL_ERROR
                "${validation_name} release source-tree validation failed with exit code ${validation_result}\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
        endif()
        return()
    endif()

    if(validation_result EQUAL 0)
        message(FATAL_ERROR
            "${validation_name} release source-tree validation unexpectedly passed\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
    endif()

    set(actual_error "${validation_stderr}")
    string(REPLACE "\r\n" "\n" actual_error "${actual_error}")
    string(REPLACE "\r" "\n" actual_error "${actual_error}")

    foreach(required_fragment IN LISTS ARGN)
        string(FIND "${actual_error}" "${required_fragment}" fragment_index)
        if(fragment_index EQUAL -1)
            message(FATAL_ERROR
                "${validation_name} release source-tree validation error did not contain required fragment '${required_fragment}'\nstdout:\n${validation_stdout}\nstderr:\n${validation_stderr}")
        endif()
    endforeach()
endfunction()

function(init_repo_with_tracked_file repo_path tracked_file contents)
    init_clean_repo("${repo_path}")
    get_filename_component(tracked_file_dir "${tracked_file}" DIRECTORY)
    if(NOT tracked_file_dir STREQUAL "")
        file(MAKE_DIRECTORY "${repo_path}/${tracked_file_dir}")
    endif()
    file(WRITE "${repo_path}/${tracked_file}" "${contents}")
    run_git("${repo_path}" add "${tracked_file}")
    run_git("${repo_path}" -c user.name=Evident -c user.email=evident@example.invalid commit -q -m "add tracked ${tracked_file}")
endfunction()

function(assert_tracked_generated_path_rejected validation_name repo_suffix tracked_file)
    set(repo_path "${work_dir}/${repo_suffix}")
    init_repo_with_tracked_file("${repo_path}" "${tracked_file}" "tracked generated output\n")
    run_release_source_tree_validation(
        "${validation_name}"
        "${repo_path}"
        FALSE
        "release source tree must not track"
        "generated build, release,"
        "local-only"
        "${tracked_file}"
    )
endfunction()

string(RANDOM LENGTH 8 ALPHABET "0123456789abcdef" test_suffix)
set(work_dir "${BUILD_DIR}/release-source-tree-${test_suffix}")
set(clean_repo "${work_dir}/clean")
set(dirty_repo "${work_dir}/dirty")
set(dirty_tracked_build_repo "${work_dir}/dirty-tracked-build")

init_clean_repo("${clean_repo}")
run_release_source_tree_validation("clean synthetic repo" "${clean_repo}" TRUE)

init_clean_repo("${dirty_repo}")
file(APPEND "${dirty_repo}/README.md" "dirty edit\n")
file(WRITE "${dirty_repo}/scratch.txt" "untracked file\n")
run_release_source_tree_validation(
    "dirty synthetic repo"
    "${dirty_repo}"
    FALSE
    "release source tree must be clean before tagging"
    "README.md"
    "scratch.txt"
)

assert_tracked_generated_path_rejected(
    "tracked build directory synthetic repo"
    "tracked-build-directory"
    "build/generated.txt"
)

assert_tracked_generated_path_rejected(
    "tracked build output synthetic repo"
    "tracked-build"
    "build-x64/generated.txt"
)

assert_tracked_generated_path_rejected(
    "tracked out directory synthetic repo"
    "tracked-out"
    "out/generated.txt"
)

assert_tracked_generated_path_rejected(
    "tracked cmake-build directory synthetic repo"
    "tracked-cmake-build"
    "cmake-build-debug/generated.txt"
)

assert_tracked_generated_path_rejected(
    "tracked user presets synthetic repo"
    "tracked-user-presets"
    "CMakeUserPresets.json"
)

assert_tracked_generated_path_rejected(
    "tracked in-source CMake cache synthetic repo"
    "tracked-cmake-cache"
    "CMakeCache.txt"
)

assert_tracked_generated_path_rejected(
    "tracked in-source CMake files synthetic repo"
    "tracked-cmake-files"
    "CMakeFiles/generated.txt"
)

assert_tracked_generated_path_rejected(
    "tracked in-source Ninja build synthetic repo"
    "tracked-ninja-build"
    "build.ninja"
)

assert_tracked_generated_path_rejected(
    "tracked in-source CTest output synthetic repo"
    "tracked-ctest-output"
    "Testing/Temporary/LastTest.log"
)

assert_tracked_generated_path_rejected(
    "tracked in-source CPack output synthetic repo"
    "tracked-cpack-output"
    "_CPack_Packages/package.txt"
)

assert_tracked_generated_path_rejected(
    "tracked release ZIP synthetic repo"
    "tracked-release-zip"
    "evident-0.1.0-windows-x64.zip"
)

assert_tracked_generated_path_rejected(
    "tracked release checksum synthetic repo"
    "tracked-release-checksum"
    "evident-0.1.0-windows-x64.zip.sha256"
)

assert_tracked_generated_path_rejected(
    "tracked release evidence synthetic repo"
    "tracked-release-evidence"
    "evident-release-evidence.txt"
)

assert_tracked_generated_path_rejected(
    "tracked downloaded release artifact synthetic repo"
    "tracked-release-artifact-directory"
    "release-artifact/evident-0.1.0-windows-x64.zip"
)

assert_tracked_generated_path_rejected(
    "tracked editor directory synthetic repo"
    "tracked-editor-directory"
    ".vscode/settings.json"
)

assert_tracked_generated_path_rejected(
    "tracked editor user file synthetic repo"
    "tracked-editor-user-file"
    "evident.suo"
)

assert_tracked_generated_path_rejected(
    "tracked compiled object synthetic repo"
    "tracked-compiled-object"
    "obj/generated.obj"
)

assert_tracked_generated_path_rejected(
    "tracked OS junk synthetic repo"
    "tracked-os-junk"
    "docs/.DS_Store"
)

assert_tracked_generated_path_rejected(
    "tracked analysis output synthetic repo"
    "tracked-analysis-output"
    "repomix-output.txt"
)

init_repo_with_tracked_file("${dirty_tracked_build_repo}" "build-x64/generated.txt" "tracked generated output\n")
file(APPEND "${dirty_tracked_build_repo}/README.md" "dirty edit\n")
run_release_source_tree_validation(
    "dirty tracked build output synthetic repo"
    "${dirty_tracked_build_repo}"
    FALSE
    "release source tree must be clean before tagging"
    "README.md"
    "release source tree must not track"
    "generated build, release,"
    "local-only"
    "build-x64/generated.txt"
)
