find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
set(_wiznote_qt_components
    Core Widgets LinguistTools Xml Network PrintSupport WebEngineCore
    WebEngineWidgets WebSockets WebChannel Svg)
if(QT_VERSION_MAJOR EQUAL 6)
    list(APPEND _wiznote_qt_components Core5Compat SvgWidgets StateMachine)
endif()
find_package(Qt${QT_VERSION_MAJOR} CONFIG REQUIRED
    COMPONENTS ${_wiznote_qt_components})

if(APPLE AND QT_VERSION_MAJOR EQUAL 5)
    find_package(Qt5MacExtras REQUIRED)
endif()

set(_qt_package_dir_var "Qt${QT_VERSION_MAJOR}_DIR")
get_filename_component(_qt_prefix "${${_qt_package_dir_var}}/../../.." ABSOLUTE)
set(qt_binary_dir "${_qt_prefix}/bin")
set(qt_translations_dir "${_qt_prefix}/translations")

# Optional workaround for restricted build sandboxes which deny Qt's sysctl
# CPU feature probe. Only host tools use Rosetta; target binaries stay native.
if(APPLE AND QT_VERSION_MAJOR EQUAL 6 AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    option(WIZNOTE_QT_HOST_TOOLS_ROSETTA
        "Run universal Qt host tools through Rosetta in restricted sandboxes" OFF)
    if(WIZNOTE_QT_HOST_TOOLS_ROSETTA)
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/qt-host-tools")
        foreach(_tool uic rcc lrelease lupdate)
            if(TARGET Qt6::${_tool})
                get_target_property(_tool_path Qt6::${_tool} IMPORTED_LOCATION)
                if(_tool_path)
                    set(_wrapper "${CMAKE_BINARY_DIR}/qt-host-tools/${_tool}")
                    file(WRITE "${_wrapper}"
                        "#!/bin/sh\nexec /usr/bin/arch -x86_64 \"${_tool_path}\" \"$@\"\n")
                    file(CHMOD "${_wrapper}" PERMISSIONS
                        OWNER_READ OWNER_WRITE OWNER_EXECUTE
                        GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
                    set_target_properties(Qt6::${_tool} PROPERTIES
                        IMPORTED_LOCATION "${_wrapper}"
                        IMPORTED_LOCATION_DEBUG "${_wrapper}"
                        IMPORTED_LOCATION_RELEASE "${_wrapper}"
                        IMPORTED_LOCATION_RELWITHDEBINFO "${_wrapper}"
                        IMPORTED_LOCATION_MINSIZEREL "${_wrapper}")
                endif()
            endif()
        endforeach()
    endif()
endif()

set(_qt_version_var "Qt${QT_VERSION_MAJOR}_VERSION")
set(QT_VERSION "${${_qt_version_var}}")

set(CMAKE_AUTOMOC ON)

macro(qt_add_translation)
    set(_files)
    foreach(_file ${ARGV})
        if(NOT ${_file} STREQUAL ${ARGV0})
            list(APPEND _files ${_file})
        endif()
    endforeach()

    if(QT_VERSION_MAJOR EQUAL 6)
        qt6_add_translation(${ARGV0} ${_files})
    else()
        qt5_add_translation(${ARGV0} ${_files})
    endif()
endmacro()

macro(qt_add_ui)
    set(_files)
    foreach(_file ${ARGV})
        if(NOT ${_file} STREQUAL ${ARGV0})
            list(APPEND _files ${_file})
        endif()
    endforeach()

    if(QT_VERSION_MAJOR EQUAL 6)
        qt6_wrap_ui(${ARGV0} ${_files})
    else()
        qt5_wrap_ui(${ARGV0} ${_files})
    endif()
endmacro()

macro(qt_add_resources)
    set(_files)
    foreach(_file ${ARGV})
        if(NOT ${_file} STREQUAL ${ARGV0})
            list(APPEND _files ${_file})
        endif()
    endforeach()
    message(STATUS ${_files})

    if(QT_VERSION_MAJOR EQUAL 6)
        qt6_add_resources(${ARGV0} ${_files})
    else()
        qt5_add_resources(${ARGV0} ${_files})
    endif()
endmacro()

macro(qt_use_modules)
    target_link_libraries( ${ARGV0} 
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Xml
        Qt${QT_VERSION_MAJOR}::Network
        Qt${QT_VERSION_MAJOR}::PrintSupport
        Qt${QT_VERSION_MAJOR}::WebEngineCore
        Qt${QT_VERSION_MAJOR}::WebEngineWidgets
        Qt${QT_VERSION_MAJOR}::WebSockets
        Qt${QT_VERSION_MAJOR}::WebChannel
        Qt${QT_VERSION_MAJOR}::Svg
    )
    if(QT_VERSION_MAJOR EQUAL 6)
        target_link_libraries(${ARGV0} Qt6::Core5Compat Qt6::SvgWidgets Qt6::StateMachine)
    endif()
    if(APPLE AND QT_VERSION_MAJOR EQUAL 5)
        target_link_libraries(${ARGV0} Qt5::MacExtras)
    elseif(WIN32)
        target_link_libraries( ${ARGV0} Qt5::WinMain)
    endif()
endmacro()

macro(qt_suppress_warnings)
    if(APPLE)
        set_target_properties(${ARGV0} PROPERTIES
            COMPILE_FLAGS "-Wno-#warnings")
    endif(APPLE)
endmacro()
