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

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
add_executable(wiznote_login_recovery_smoke login-window-recovery.cpp)
target_include_directories(wiznote_login_recovery_smoke PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(wiznote_login_recovery_smoke PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
add_test(NAME wiznote_login_recovery_smoke COMMAND wiznote_login_recovery_smoke)
set_tests_properties(wiznote_login_recovery_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

target_compile_definitions(WizNotePlus PRIVATE WIZNOTE_NATIVE_TRANSFER_TESTS)
add_test(NAME wiznote_advanced_search
    COMMAND $<TARGET_FILE:WizNotePlus> --advanced-search-self-test)
set_tests_properties(wiznote_advanced_search PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
add_test(NAME wiznote_native_note_transfer
    COMMAND $<TARGET_FILE:WizNotePlus> --native-note-transfer-self-test)
set_tests_properties(wiznote_native_note_transfer PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
add_test(NAME wiznote_note_presentation
    COMMAND $<TARGET_FILE:WizNotePlus> --note-presentation-self-test)
set_tests_properties(wiznote_note_presentation PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)
