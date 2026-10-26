find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(_wiznote_component_verifier "${PROJECT_SOURCE_DIR}/build/verify-components.py")
set(_wiznote_component_lock "${PROJECT_SOURCE_DIR}/build/components.lock.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_wiznote_component_verifier}" "${_wiznote_component_lock}")

function(wiznote_verify_source_components)
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${_wiznote_component_verifier}"
        --source-root "${PROJECT_SOURCE_DIR}" --qt-version "${QT_VERSION}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Component versions do not match the reviewed default set:\n${output}${error}")
    endif()
    message(STATUS "${output}")
endfunction()

function(wiznote_add_component_check target)
    add_custom_target(wiznote_check_components
        COMMAND "${Python3_EXECUTABLE}" "${_wiznote_component_verifier}"
            --source-root "${PROJECT_SOURCE_DIR}"
            --resource-root "${WIZNOTE_RESOURCE_DIR}/files"
            --qt-version "${QT_VERSION}"
        VERBATIM COMMENT "Checking pinned source and copied component versions")
    add_dependencies(${target} wiznote_check_components)
endfunction()
