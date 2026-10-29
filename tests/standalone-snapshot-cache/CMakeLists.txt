cmake_minimum_required(VERSION 3.24)
project(WizSnapshotCacheRetirement LANGUAGES CXX)
if(NOT APPLE)
    message(FATAL_ERROR "This independent allocation diagnostic requires macOS malloc counters")
endif()
find_package(Qt6 6.11.3 EXACT REQUIRED COMPONENTS Core)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(WIZNOTE_CACHE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/gui/documentviewer/WizDocumentWebView.cpp")
set(WIZNOTE_SAVER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../src/gui/documentviewer/DocumentLoaderSaver.cpp")
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/before-clear.inc" "${CMAKE_CURRENT_BINARY_DIR}/current-clear.inc" "${CMAKE_CURRENT_BINARY_DIR}/saver-handoff.inc" "${CMAKE_CURRENT_BINARY_DIR}/method-source.json"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract-clear.py"
        --source "${WIZNOTE_CACHE_SOURCE}"
        --baseline "${CMAKE_CURRENT_SOURCE_DIR}/before-clear.fixture.cpp"
        --saver "${WIZNOTE_SAVER_SOURCE}"
        --output-dir "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS "${WIZNOTE_CACHE_SOURCE}" "${WIZNOTE_SAVER_SOURCE}" "${CMAKE_CURRENT_SOURCE_DIR}/before-clear.fixture.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/extract-clear.py"
    VERBATIM)
add_executable(cache-probe main.cpp "${CMAKE_CURRENT_BINARY_DIR}/current-clear.inc" "${CMAKE_CURRENT_BINARY_DIR}/before-clear.inc" "${CMAKE_CURRENT_BINARY_DIR}/saver-handoff.inc")
target_include_directories(cache-probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(cache-probe PRIVATE Qt6::Core)
target_compile_features(cache-probe PRIVATE cxx_std_17)
enable_testing()
add_test(NAME snapshot_cache_retirement COMMAND cache-probe)
set_tests_properties(snapshot_cache_retirement PROPERTIES TIMEOUT 20)

set(WIZNOTE_PATH_METHODS "${CMAKE_CURRENT_BINARY_DIR}/path-methods")
add_custom_command(
    OUTPUT "${WIZNOTE_PATH_METHODS}/before-load.inc" "${WIZNOTE_PATH_METHODS}/current-load.inc" "${WIZNOTE_PATH_METHODS}/saver-handoff.inc" "${WIZNOTE_PATH_METHODS}/method-source.json"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract-load.py"
        --source "${WIZNOTE_CACHE_SOURCE}"
        --baseline "${CMAKE_CURRENT_SOURCE_DIR}/before-load.fixture.cpp"
        --saver "${WIZNOTE_SAVER_SOURCE}"
        --output-dir "${WIZNOTE_PATH_METHODS}"
    DEPENDS "${WIZNOTE_CACHE_SOURCE}" "${WIZNOTE_SAVER_SOURCE}" "${CMAKE_CURRENT_SOURCE_DIR}/before-load.fixture.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/extract-load.py" "${CMAKE_CURRENT_SOURCE_DIR}/extract-clear.py"
    VERBATIM)
add_executable(path-probe path-probe.cpp "${WIZNOTE_PATH_METHODS}/before-load.inc" "${WIZNOTE_PATH_METHODS}/current-load.inc" "${WIZNOTE_PATH_METHODS}/saver-handoff.inc")
target_include_directories(path-probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src" "${WIZNOTE_PATH_METHODS}")
target_link_libraries(path-probe PRIVATE Qt6::Core)
target_compile_features(path-probe PRIVATE cxx_std_17)
add_test(NAME source_path_index_bound COMMAND path-probe)
set_tests_properties(source_path_index_bound PROPERTIES TIMEOUT 20)
