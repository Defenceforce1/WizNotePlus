# Native Apple Silicon builds: see docs/macos-arm64.md.

# CMake arguments for WizNote
# APPSTORE_BUILD  build for Apple Appsotre, would not use qt deploy
# XCODEBUILD      adjust project params to suit for xcode
# UPDATE_TRANSLATIONS  update transation files
# PLCrashReporter   use PLCrashReporter for mac, need PLCrashReporter framework

cmake_minimum_required(VERSION 3.21)

if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 14.0 CACHE STRING "Minimum supported macOS version")
endif()

project(WizNotePlus VERSION 2.12.0)

# Independent trial identity; keep the upstream client version and note schema.
set(WIZNOTE_COMPAT_TRIAL_BUILD "14.4.25")
set(WIZNOTE_APPLE_SILICON_TRIAL 0)
set(WIZNOTE_MAC_DISPLAY_NAME "WizNotePlus")
set(WIZNOTE_MAC_BUNDLE_BUILD "${PROJECT_VERSION}")
set(WIZNOTE_MAC_BUNDLE_ID "cn.wiz.wiznoteformac")
set(WIZNOTE_MAC_BUNDLE_NAME "WizNotePlus")
set(WIZNOTE_MAC_URL_TYPES "<array><dict><key>CFBundleURLName</key><string>WizNote url scheme</string><key>CFBundleURLSchemes</key><array><string>wiz</string></array></dict></array>")
if(APPLE AND CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    set(WIZNOTE_APPLE_SILICON_TRIAL 1)
    set(WIZNOTE_MAC_DISPLAY_NAME "WizNotePlus Apple Silicon")
    set(WIZNOTE_MAC_BUNDLE_BUILD "${WIZNOTE_COMPAT_TRIAL_BUILD}")
    set(WIZNOTE_MAC_BUNDLE_ID "io.github.defenceforce1.wiznoteplus.applesilicon")
    set(WIZNOTE_MAC_BUNDLE_NAME "WizNotePlus Apple Silicon")
    # Keep in-note wiz:// links, but do not take over old-app LaunchServices URLs.
    set(WIZNOTE_MAC_URL_TYPES "<array/>")
endif()

#============================================================================
# Options and Variables
#============================================================================

option(BUILD_TESTING "Building the test target or not." OFF)
option(USE_CUSTOM_SCROLLBAR "Use custom scrollbar instead of Qt's one." ON)

# Excute conan outside or not.
option(CONAN_INSTALL_MANUALLY "Run Conan install command manually." OFF)
if(APPLE)
    option(WIZNOTE_USE_CONAN "Use the legacy Conan 1 dependency workflow" OFF)
else()
    option(WIZNOTE_USE_CONAN "Use the legacy Conan 1 dependency workflow" ON)
endif()
option(WIZNOTE_BUILD_COMPAT_TESTS "Build isolated Qt/data compatibility smoke tests" OFF)
option(WIZNOTE_ENABLE_MARKDOWN_RECOVERY "Experimental same-target Markdown recovery App wiring" OFF)

# Set default build type to Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
      "Choose the type of build, options are: Debug Release."
      FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH 
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
)

# WizNotePlus Dirs
if(APPLE)
    set(WIZNOTE_ROOT_DIR ${CMAKE_BINARY_DIR}/bin/WizNotePlus.app)
    set(WIZNOTE_CONTENTS_DIR ${WIZNOTE_ROOT_DIR}/Contents)
    set(WIZNOTE_RESOURCE_DIR ${WIZNOTE_ROOT_DIR}/Contents/Resources)
else(APPLE)
    set(WIZNOTE_ROOT_DIR ${CMAKE_BINARY_DIR})
    set(WIZNOTE_RESOURCE_DIR ${WIZNOTE_ROOT_DIR}/share/wiznote)
endif(APPLE)

#============================================================================
# RPATH settings
#============================================================================

# rpath resolve.
#set(CMAKE_BUILD_WITH_INSTALL_RPATH YES)     // would cause wheelEvent problem on centos/fedora when compiled with qt5
if(APPLE)
    # make the install names for dylibs include "@rpath/" prefix.
    set(CMAKE_MACOSX_RPATH ON)
    set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    set(CMAKE_INSTALL_NAME_DIR "@rpath/PlugIns")
    set(CMAKE_INSTALL_RPATH "@loader_path/..;@loader_path/../Frameworks;@executable_path/..;@executable_path/../Frameworks;@rpath")
    #SET(PLCrashReporter on)
else(APPLE)
    set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    set(CMAKE_INSTALL_RPATH "$ORIGIN;$ORIGIN/..;$ORIGIN/../lib")
    set (openssl-linked on)
endif(APPLE)

#============================================================================
# 3rd-party libraries
#============================================================================

# install conan 3rdparty libraries to package dirs.
message("\nStart generate 3rdparty dependencies as ${CMAKE_BUILD_TYPE}:\n")

if(WIZNOTE_USE_CONAN AND NOT CONAN_INSTALL_MANUALLY)
    # if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
    #     message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
    #     file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/master/conan.cmake"
    #                     "${CMAKE_BINARY_DIR}/conan.cmake")
    # endif()
    include(conan)
    conan_check(REQUIRED)
    conan_cmake_run(
        CONANFILE conanfile.py
        INSTALL_FOLDER ${CMAKE_BINARY_DIR}
        BUILD_TYPE ${CMAKE_BUILD_TYPE}
        BUILD missing
    )
    include(${CMAKE_BINARY_DIR}/conan_paths.cmake)
endif()

# set find_package search path
# This is a workaround of conan-zlib bug https://github.com/conan-community/community/issues/259
set(CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR} ${CMAKE_MODULE_PATH})

# setup 3rd-party package
find_package(cryptopp REQUIRED)
find_package(ZLIB REQUIRED)
if(NOT APPLE)
    find_package(OpenSSL)
endif()

# install 3rd-party package
# list(REMOVE_ITEM OpenSSL_LIBRARIES dl)
# if(NOT APPLE)
#     set(lib_install_dest lib)
# else()
#     set(lib_install_dest Contents/MacOS)
# endif()
# install(DIRECTORY ${CMAKE_BINARY_DIR}/lib/ DESTINATION ${lib_install_dest} )

# set output path
if(WIZNOTE_USE_CONAN)
    include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
    conan_output_dirs_setup()
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
endif()

find_package(Git QUIET)

macro(git_submodule)
    foreach(_module ${ARGV})
        message(STATUS "Submodule updating ${_module}")
        execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init ${_module}
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "Updating ${_module} failed with ${GIT_SUBMOD_RESULT}.")
        endif()
    endforeach()
endmacro()

if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    git_submodule(
        src/libs/3rdparty/framelesshelper
        src/libs/3rdparty/quazip
        src/libs/3rdparty/gumbo-parser
    )
endif()

# Keep the pinned upstream submodule, with a reproducible Qt 6 compatibility patch.
include(ApplyFramelessHelperPatch)

#============================================================================
# Internal compiler options
#============================================================================

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(UNIX)
    if(COMPILER_SUPPORTS_CXX11)
        if(APPLE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
        else(APPLE)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
        endif(APPLE)
    elseif(COMPILER_SUPPORTS_CXX0X)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
    else()
        message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
    endif()
endif()

if(APPLE)
    if(APPSTORE_BUILD)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -gdwarf-2")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -gdwarf-2")
        add_definitions(-DBUILD4APPSTORE=TRUE)
    endif(APPSTORE_BUILD)
    if (PLCrashReporter)
        add_definitions(-DUsePLCrashReporter=TRUE)
    endif(PLCrashReporter)
    add_definitions(-DUSECOCOATOOLBAR=TRUE)
endif(APPLE)

if (USE_CUSTOM_SCROLLBAR)
    add_definitions(-DWIZNOTE_CUSTOM_SCROLLBAR)
endif(USE_CUSTOM_SCROLLBAR)

#============================================================================
# Subfolders
#============================================================================

# The pinned FramelessHelper uses Qt's private GUI/Widgets headers. Starting
# with Qt 6.10 these are separate packages, not implicit public-module targets.
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
if(QT_VERSION_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS Gui Widgets)
    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.10)
        find_package(Qt6 REQUIRED COMPONENTS GuiPrivate WidgetsPrivate)
    endif()
endif()

#add_subdirectory(lib)
add_subdirectory(src)

if(WIZNOTE_BUILD_COMPAT_TESTS)
    enable_testing()
    add_subdirectory(tests/compat)
endif()

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
