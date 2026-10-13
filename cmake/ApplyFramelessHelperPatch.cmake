set(_frameless_dir "${PROJECT_SOURCE_DIR}/src/libs/3rdparty/framelesshelper")
set(_frameless_patch "${PROJECT_SOURCE_DIR}/cmake/patches/framelesshelper-qt6.patch")
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${_frameless_patch}"
        WORKING_DIRECTORY "${_frameless_dir}" RESULT_VARIABLE _already_patched
        OUTPUT_QUIET ERROR_QUIET)
    if(NOT _already_patched EQUAL 0)
        execute_process(COMMAND "${GIT_EXECUTABLE}" apply --check "${_frameless_patch}"
            WORKING_DIRECTORY "${_frameless_dir}" RESULT_VARIABLE _can_patch)
        if(NOT _can_patch EQUAL 0)
            message(FATAL_ERROR "Cannot apply the pinned FramelessHelper compatibility patch")
        endif()
        execute_process(COMMAND "${GIT_EXECUTABLE}" apply "${_frameless_patch}"
            WORKING_DIRECTORY "${_frameless_dir}" RESULT_VARIABLE _patch_result)
        if(NOT _patch_result EQUAL 0)
            message(FATAL_ERROR "FramelessHelper compatibility patch failed")
        endif()
    endif()
endif()
