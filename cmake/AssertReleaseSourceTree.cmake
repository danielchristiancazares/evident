if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

if(NOT DEFINED REQUIRE_CLEAN_WORKTREE)
    set(REQUIRE_CLEAN_WORKTREE TRUE)
endif()

find_program(GIT_EXECUTABLE git)
if(NOT GIT_EXECUTABLE)
    message(FATAL_ERROR "git is required to validate the release source tree")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" rev-parse --show-toplevel
    RESULT_VARIABLE git_root_result
    OUTPUT_VARIABLE git_root
    ERROR_VARIABLE git_root_error
)
if(NOT git_root_result EQUAL 0)
    message(FATAL_ERROR
        "SOURCE_DIR must be inside a Git worktree: ${SOURCE_DIR}\n${git_root_error}")
endif()
string(STRIP "${git_root}" git_root)

set(release_source_tree_error_message "")

if(REQUIRE_CLEAN_WORKTREE)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${git_root}" status --porcelain=v1 --untracked-files=all
        RESULT_VARIABLE git_status_result
        OUTPUT_VARIABLE git_status_output
        ERROR_VARIABLE git_status_error
    )
    if(NOT git_status_result EQUAL 0)
        message(FATAL_ERROR
            "failed to inspect release source tree status: ${git_root}\n${git_status_error}")
    endif()

    string(REPLACE "\r\n" "\n" git_status_output "${git_status_output}")
    string(REPLACE "\r" "\n" git_status_output "${git_status_output}")
    string(STRIP "${git_status_output}" git_status_output)
    if(NOT git_status_output STREQUAL "")
        string(APPEND release_source_tree_error_message
            "release source tree must be clean before tagging: ${git_root}\n${git_status_output}")
    endif()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${git_root}" ls-files
    RESULT_VARIABLE git_ls_files_result
    OUTPUT_VARIABLE tracked_files_output
    ERROR_VARIABLE git_ls_files_error
)
if(NOT git_ls_files_result EQUAL 0)
    message(FATAL_ERROR
        "failed to inspect tracked release source tree files: ${git_root}\n${git_ls_files_error}")
endif()

string(REPLACE "\r\n" "\n" tracked_files_output "${tracked_files_output}")
string(REPLACE "\r" "\n" tracked_files_output "${tracked_files_output}")
string(REPLACE "\n" ";" tracked_files "${tracked_files_output}")

set(tracked_generated_outputs)
foreach(tracked_file IN LISTS tracked_files)
    if(tracked_file STREQUAL "")
        continue()
    endif()

    string(REPLACE "\\" "/" normalized_tracked_file "${tracked_file}")
    if(normalized_tracked_file MATCHES "^(build/|build-.+(/|$)|out/|cmake-build-.+(/|$)|CMakeUserPresets\\.json$|CMakeCache\\.txt$|CMakeFiles/|CTestTestfile\\.cmake$|DartConfiguration\\.tcl$|Testing/|cmake_install\\.cmake$|compile_commands\\.json$|install_manifest\\.txt$|build\\.ninja$|\\.ninja_deps$|\\.ninja_log$|_CPack_Packages/|CPackConfig\\.cmake$|CPackSourceConfig\\.cmake$|evident-[^/]+-windows-x64\\.zip(\\.sha256)?$|evident-release-evidence\\.txt$|release-artifact/|(.*/)?\\.(vs|vscode|minimax|idea)/|(.*/)?[^/]+\\.(suo|user|sln\\.docstates|swp|o|obj|pch|gch)$|(.*/)?[^/]+~$|(.*/)?(Thumbs\\.db|Desktop\\.ini|\\.DS_Store|repomix-output\\.txt)$)")
        list(APPEND tracked_generated_outputs "${normalized_tracked_file}")
    endif()
endforeach()

if(tracked_generated_outputs)
    list(SORT tracked_generated_outputs)
    string(REPLACE ";" "\n  " formatted_tracked_generated_outputs "${tracked_generated_outputs}")
    if(NOT release_source_tree_error_message STREQUAL "")
        string(APPEND release_source_tree_error_message "\n\n")
    endif()
    string(APPEND release_source_tree_error_message
        "release source tree must not track generated build, release, or local-only output; remove these paths from source control before release:\n  ${formatted_tracked_generated_outputs}")
endif()

if(NOT release_source_tree_error_message STREQUAL "")
    message(FATAL_ERROR
        "${release_source_tree_error_message}")
endif()
