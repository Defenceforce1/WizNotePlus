find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME wiznote_app_language_resources
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/app-language-resources.py)
set_tests_properties(wiznote_app_language_resources PROPERTIES
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1" TIMEOUT 30)
if(QT_VERSION_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS Core5Compat)
endif()
add_executable(wiznote_thread_pool thread-pool.cpp
    ${PROJECT_SOURCE_DIR}/src/share/WizThreads.cpp
    ${PROJECT_SOURCE_DIR}/src/share/WizThreads_p.h)
target_include_directories(wiznote_thread_pool PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(wiznote_thread_pool PRIVATE WIZNOTE_THREAD_POOL_TEST_HOOKS)
set_target_properties(wiznote_thread_pool PROPERTIES AUTOMOC ON)
target_link_libraries(wiznote_thread_pool PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui)
if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(wiznote_thread_pool PRIVATE Qt6::Core5Compat)
endif()
add_test(NAME wiznote_thread_pool COMMAND wiznote_thread_pool)
set_tests_properties(wiznote_thread_pool PROPERTIES TIMEOUT 30)
add_executable(wiznote_document_owner_actions document-owner-actions.cpp
    ${PROJECT_SOURCE_DIR}/src/share/WizThreads.cpp
    ${PROJECT_SOURCE_DIR}/src/share/WizThreads_p.h)
target_include_directories(wiznote_document_owner_actions PRIVATE ${PROJECT_SOURCE_DIR}/src)
set_target_properties(wiznote_document_owner_actions PROPERTIES AUTOMOC ON)
target_link_libraries(wiznote_document_owner_actions PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui)
if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(wiznote_document_owner_actions PRIVATE Qt6::Core5Compat)
endif()
add_test(NAME wiznote_document_owner_actions COMMAND wiznote_document_owner_actions)
set_tests_properties(wiznote_document_owner_actions PROPERTIES TIMEOUT 30)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Network)
add_executable(wiznote_async_api_lifetime async-api-lifetime.cpp
    ${PROJECT_SOURCE_DIR}/src/sync/WizAsyncApi.cpp
    ${PROJECT_SOURCE_DIR}/src/sync/WizAsyncApi.h
    ${PROJECT_SOURCE_DIR}/src/share/WizThreads.cpp
    ${PROJECT_SOURCE_DIR}/src/share/WizThreads_p.h)
set_target_properties(wiznote_async_api_lifetime PROPERTIES AUTOMOC ON)
target_include_directories(wiznote_async_api_lifetime PRIVATE
    ${PROJECT_SOURCE_DIR}/src/sync ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
target_compile_definitions(wiznote_async_api_lifetime PRIVATE WIZNOTE_ASYNC_API_TEST_TRANSPORT)
target_link_libraries(wiznote_async_api_lifetime PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Network)
if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(wiznote_async_api_lifetime PRIVATE Qt6::Core5Compat)
endif()
add_test(NAME wiznote_async_api_lifetime COMMAND wiznote_async_api_lifetime)
set_tests_properties(wiznote_async_api_lifetime PROPERTIES TIMEOUT 60)
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
add_executable(wiznote_pdf_paper_page pdf-paper-page.cpp)
target_include_directories(wiznote_pdf_paper_page PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(wiznote_pdf_paper_page PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::WebEngineCore Qt${QT_VERSION_MAJOR}::WebEngineWidgets)
add_test(NAME wiznote_pdf_paper_page COMMAND wiznote_pdf_paper_page)
set_tests_properties(wiznote_pdf_paper_page PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
add_executable(wiznote_pdf_export_menu pdf-export-menu.cpp)
target_include_directories(wiznote_pdf_export_menu PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(wiznote_pdf_export_menu PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
add_test(NAME wiznote_pdf_export_menu COMMAND wiznote_pdf_export_menu)
set_tests_properties(wiznote_pdf_export_menu PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
add_executable(wiznote_synthetic_input_guard synthetic-input-guard.cpp)
target_link_libraries(wiznote_synthetic_input_guard PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
add_test(NAME wiznote_synthetic_input_guard COMMAND wiznote_synthetic_input_guard)
set_tests_properties(wiznote_synthetic_input_guard PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
add_executable(wiznote_pdf_export_lifecycle pdf-export-lifecycle.cpp)
target_include_directories(wiznote_pdf_export_lifecycle PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(wiznote_pdf_export_lifecycle PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::WebEngineCore Qt${QT_VERSION_MAJOR}::WebEngineWidgets)
add_test(NAME wiznote_pdf_export_lifecycle COMMAND wiznote_pdf_export_lifecycle)
set_tests_properties(wiznote_pdf_export_lifecycle PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)

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
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS QuickWidgets)
target_link_libraries(WizNotePlus Qt${QT_VERSION_MAJOR}::QuickWidgets)
if(APPLE AND QT_VERSION_MAJOR EQUAL 6 AND Qt6Core_VERSION VERSION_GREATER_EQUAL "6.6" AND TARGET Qt6::GuiPrivate)
    add_library(wiznote_memory_rhi STATIC native-memory-rhi.cpp)
    target_link_libraries(wiznote_memory_rhi PRIVATE Qt6::QuickWidgets Qt6::GuiPrivate)
    target_link_libraries(WizNotePlus wiznote_memory_rhi)
    set_property(SOURCE ${PROJECT_SOURCE_DIR}/tests/compat/native-editor-runtime.cpp TARGET_DIRECTORY WizNotePlus
        APPEND PROPERTY COMPILE_DEFINITIONS WIZNOTE_MEMORY_RHI_TESTS)
endif()
add_test(NAME wiznote_comment_lifetime COMMAND $<TARGET_FILE:WizNotePlus> --comment-lifetime-self-test)
set_tests_properties(wiznote_comment_lifetime PROPERTIES TIMEOUT 30)
add_test(NAME wiznote_certificate_creation COMMAND $<TARGET_FILE:WizNotePlus> --certificate-creation-self-test)
set_tests_properties(wiznote_certificate_creation PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
add_test(NAME wiznote_certificate_creation_ui COMMAND $<TARGET_FILE:WizNotePlus> --certificate-creation-ui-self-test)
set_tests_properties(wiznote_certificate_creation_ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
add_test(NAME wiznote_biometric_keychain COMMAND $<TARGET_FILE:WizNotePlus> --biometric-keychain-self-test)
add_test(NAME wiznote_biometric_unlock_ui COMMAND $<TARGET_FILE:WizNotePlus> --biometric-unlock-ui-self-test)
add_test(NAME wiznote_biometric_note_access COMMAND $<TARGET_FILE:WizNotePlus> --biometric-note-access-self-test)
add_test(NAME wiznote_document_unprotect COMMAND $<TARGET_FILE:WizNotePlus> --document-unprotect-self-test)
set_tests_properties(wiznote_document_unprotect PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
set_tests_properties(wiznote_biometric_note_access PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
add_test(NAME wiznote_biometric_document_routing COMMAND $<TARGET_FILE:WizNotePlus> --biometric-document-routing-self-test)
set_tests_properties(wiznote_biometric_document_routing PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
set_tests_properties(wiznote_biometric_unlock_ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
set_tests_properties(wiznote_biometric_keychain PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
add_test(NAME wiznote_settings_ui COMMAND $<TARGET_FILE:WizNotePlus> --settings-ui-self-test)
set_tests_properties(wiznote_settings_ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
add_test(NAME wiznote_account_file_selection COMMAND $<TARGET_FILE:WizNotePlus> --account-file-selection-self-test)
set_tests_properties(wiznote_account_file_selection PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
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
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;WIZNOTE_VERIFY_PERSISTED_RENDERING=1" TIMEOUT 300)
add_test(NAME wiznote_native_editor_runtime
    COMMAND $<TARGET_FILE:WizNotePlus> --native-editor-runtime-self-test)
set_tests_properties(wiznote_native_editor_runtime PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
