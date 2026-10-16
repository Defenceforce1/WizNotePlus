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

add_executable(wiznote_pdf_file_safety pdf-file-safety.cpp)
target_include_directories(wiznote_pdf_file_safety PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(wiznote_pdf_file_safety PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui)
add_test(NAME wiznote_pdf_file_safety COMMAND wiznote_pdf_file_safety)
set_tests_properties(wiznote_pdf_file_safety PROPERTIES TIMEOUT 30)

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets WebEngineCore WebEngineWidgets)
add_executable(wiznote_document_async document-async.cpp)
target_include_directories(wiznote_document_async PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(wiznote_document_async PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::WebEngineCore Qt${QT_VERSION_MAJOR}::WebEngineWidgets)
add_test(NAME wiznote_document_async COMMAND wiznote_document_async)
set_tests_properties(wiznote_document_async PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

add_executable(wiznote_login_recovery_smoke login-window-recovery.cpp)
target_include_directories(wiznote_login_recovery_smoke PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(wiznote_login_recovery_smoke PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
add_test(NAME wiznote_login_recovery_smoke COMMAND wiznote_login_recovery_smoke)
set_tests_properties(wiznote_login_recovery_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

target_compile_definitions(WizNotePlus PRIVATE WIZNOTE_NATIVE_TRANSFER_TESTS)
add_test(NAME wiznote_comment_lifetime COMMAND $<TARGET_FILE:WizNotePlus> --comment-lifetime-self-test)
set_tests_properties(wiznote_comment_lifetime PROPERTIES TIMEOUT 30)
add_test(NAME wiznote_spellcheck COMMAND $<TARGET_FILE:WizNotePlus> --spellcheck-self-test)
set_tests_properties(wiznote_spellcheck PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa" TIMEOUT 120)
add_test(NAME wiznote_native_clipper COMMAND $<TARGET_FILE:WizNotePlus> --native-clipper-self-test)
set_tests_properties(wiznote_native_clipper PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
add_test(NAME wiznote_shutdown COMMAND $<TARGET_FILE:WizNotePlus> --shutdown-self-test)
set_tests_properties(wiznote_shutdown PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
find_program(WIZNOTE_TEST_OPENSSL openssl REQUIRED)
add_test(NAME wiznote_network_security
    COMMAND $<TARGET_FILE:WizNotePlus> --network-security-self-test ${WIZNOTE_TEST_OPENSSL})
set_tests_properties(wiznote_network_security PROPERTIES TIMEOUT 180)
add_test(NAME wiznote_standalone_startup
    COMMAND $<TARGET_FILE:WizNotePlus> --standalone-startup-self-test)
set_tests_properties(wiznote_standalone_startup PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
add_test(NAME wiznote_standalone_bootstrap_cancel
    COMMAND $<TARGET_FILE:WizNotePlus> --standalone-bootstrap-cancel-probe)
set_tests_properties(wiznote_standalone_bootstrap_cancel PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
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
