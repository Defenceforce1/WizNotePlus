cmake_minimum_required(VERSION 3.24)
project(StandaloneQtPdf LANGUAGES CXX)
# Diagnostic only; no App bootstrap, account, storage or packaged runtime.
find_package(Qt6 6.6 REQUIRED COMPONENTS Widgets WebEngineWidgets)
add_executable(native-pdf ../compat/explore-native-pdf.cpp)
target_compile_features(native-pdf PRIVATE cxx_std_17)
target_include_directories(native-pdf PRIVATE ../../src)
target_link_libraries(native-pdf PRIVATE Qt6::Widgets Qt6::WebEngineWidgets)

# Smaller geometry reproducer: no Wiz code, headers, editor, math or scripts.
add_executable(anchor-geometry anchor-geometry.cpp)
target_compile_features(anchor-geometry PRIVATE cxx_std_17)
target_link_libraries(anchor-geometry PRIVATE Qt6::Widgets Qt6::WebEngineWidgets)
