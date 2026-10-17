cmake_minimum_required(VERSION 3.24)
project(StandaloneQtMemory LANGUAGES CXX)
# Deliberately independent: no add_subdirectory of the App, Wiz headers,
# generated identity, resource collection, bridge or profile factory.
find_package(Qt6 6.6 REQUIRED COMPONENTS Widgets WebEngineWidgets QuickWidgets)
add_executable(standalone-qt-memory main.cpp)
target_compile_features(standalone-qt-memory PRIVATE cxx_std_17)
target_link_libraries(standalone-qt-memory PRIVATE
    Qt6::Widgets Qt6::WebEngineWidgets Qt6::QuickWidgets)
