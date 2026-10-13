find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui)
if(QT_VERSION_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS Core5Compat)
endif()
add_executable(wiznote_compat_smoke main.cpp)
target_include_directories(wiznote_compat_smoke PRIVATE
    ${PROJECT_SOURCE_DIR}/src ${cryptopp_INCLUDE_DIRS}
    $<TARGET_PROPERTY:Qt${QT_VERSION_MAJOR}::Gui,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(wiznote_compat_smoke PRIVATE Qt${QT_VERSION_MAJOR}::Core
    QuaZip::QuaZip ${cryptopp_LIBRARIES})
if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(wiznote_compat_smoke PRIVATE Qt6::Core5Compat)
endif()
add_test(NAME wiznote_compat_smoke COMMAND wiznote_compat_smoke)
