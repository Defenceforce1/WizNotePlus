cmake_minimum_required(VERSION 3.24)
project(StandaloneMarkdownExport LANGUAGES CXX)
find_package(Qt6 6.9 REQUIRED COMPONENTS Widgets WebEngineCore Core5Compat)

# Compile the current export method, not a manually transcribed approximation.
function(extract_between path start_marker end_marker output)
  file(READ "${path}" source)
  string(REPLACE "\r\n" "\n" source "${source}")
  string(FIND "${source}" "${start_marker}" begin)
  string(FIND "${source}" "${end_marker}" end)
  if(begin LESS 0 OR end LESS begin)
    message(FATAL_ERROR "Source boundaries changed: ${path}; review this diagnostic")
  endif()
  math(EXPR length "${end} - ${begin}")
  string(SUBSTRING "${source}" ${begin} ${length} body)
  set(${output} "${body}" PARENT_SCOPE)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
endfunction()
set(root "${CMAKE_CURRENT_SOURCE_DIR}/../..")
extract_between("${root}/src/gui/documentviewer/WizDocumentWebView.cpp"
  "void WizDocumentWebView::saveAsMarkdown(QString&" "void WizDocumentWebView::saveAsPlainMarkdown(" export_body)
string(REPLACE "WizDocumentWebView" "ExportHarness" export_body "${export_body}")
extract_between("${root}/src/share/WizMisc.cpp"
  "bool WizSaveUnicodeTextToUtf8File(const QString& strFileName, const QString& strText, bool addBom)"
  "bool WizSaveUnicodeTextToData(" writer_body)
string(REPLACE "bool WizSaveUnicodeTextToUtf8File(" "bool rawWriteUtf8UnderTest(" writer_body "${writer_body}")
extract_between("${root}/src/utils/WizMisc.cpp"
  "QString WizMisc::extractFileName(" "CString WizMisc::extractTitleTemplate(" names_body)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/actual-flow.inc"
  "${writer_body}\nnamespace Utils {\n${names_body}\n}\n${export_body}\n")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/candidate-body.inc" candidate_body)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/candidate-body.inc")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/candidate-flow.inc"
  "${writer_body}\nnamespace Utils {\n${names_body}\n}\n${candidate_body}\n")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/legacy-body.inc" legacy_body)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/legacy-body.inc")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/legacy-flow.inc"
  "${writer_body}\nnamespace Utils {\n${names_body}\n}\n${legacy_body}\n")
file(SHA256 "${root}/src/gui/documentviewer/WizDocumentWebView.cpp" export_sha)
file(SHA256 "${root}/src/share/WizMisc.cpp" writer_sha)
file(SHA256 "${root}/src/utils/WizMisc.cpp" names_sha)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/candidate-body.inc" candidate_sha)
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/legacy-body.inc" legacy_sha)
option(WIZ_MARKDOWN_EXPORT_SANITIZE "Instrument own source-flow adapter; Qt remains uninstrumented" OFF)
foreach(mode actual candidate legacy)
  if(mode STREQUAL "actual")
    set(target markdown-export-probe)
  elseif(mode STREQUAL "candidate")
    set(target markdown-export-candidate)
  else()
    set(target markdown-export-legacy)
  endif()
  add_executable(${target} main.cpp)
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${root}/src" "${root}/src/share")
  target_link_libraries(${target} PRIVATE Qt6::Widgets Qt6::WebEngineCore Qt6::Core5Compat)
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_compile_definitions(${target} PRIVATE
    EXPORT_SOURCE_SHA="${export_sha}" WRITER_SOURCE_SHA="${writer_sha}" NAMES_SOURCE_SHA="${names_sha}"
    EXPORT_MODE="${mode}" CANDIDATE_SOURCE_SHA="${candidate_sha}" LEGACY_SOURCE_SHA="${legacy_sha}")
  if(mode STREQUAL "candidate")
    target_compile_definitions(${target} PRIVATE EXPORT_CANDIDATE=1)
  elseif(mode STREQUAL "legacy")
    target_compile_definitions(${target} PRIVATE EXPORT_LEGACY=1)
  else()
    target_sources(${target} PRIVATE "${root}/src/share/WizMarkdownExportTransaction.cpp")
    target_compile_definitions(${target} PRIVATE EXPORT_ACTUAL_REPLY_FIRST=1 EXPORT_PRODUCT_TRANSACTION=1)
  endif()
  if(WIZ_MARKDOWN_EXPORT_SANITIZE)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endforeach()

# Independent actual filesystem/export-method path. Archive extraction alone is
# an adapter over a synthetic directory; no database engine or archive reader.
extract_between("${root}/src/share/WizMisc.cpp" "bool WizCopyFolder(" "void WizShowDocumentHistory(" copy_body)
extract_between("${root}/src/share/WizMisc.cpp" "bool WizLoadUnicodeTextFromFile(" "bool WizLoadTextFromResource(" reader_body)
extract_between("${root}/src/share/WizMisc.cpp"
  "bool WizSaveUnicodeTextToUtf8File(const QString& strFileName, const QString& strText)"
  "bool WizSaveUnicodeTextToUtf8File(const QString& strFileName, const QByteArray& strText)" writer_two_body)
extract_between("${root}/src/utils/WizMisc.cpp" "CString WizMisc::extractFilePath(" "CString WizMisc::extractLastPathName(" path_body)
extract_between("${root}/src/database/WizDatabase.cpp" "bool WizDatabase::exportToHtmlFile(" "bool WizDatabase::extractZiwFileToFolder(" database_body)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/actual-resources.inc"
  "#include \"WizHtmlResourceUrl.h\"\n${copy_body}\n${reader_body}\n${writer_two_body}\nnamespace Utils {\n${names_body}\n${path_body}\n}\n${database_body}\n")

# Actual local WebEngine resolution of synthetic exported HTML/resources. No
# account, network, normal app, screen capture or native dialog is involved.
add_executable(html-resource-browser-probe html-resource-browser.cpp
  "${root}/src/share/WizMarkdownExportTransaction.cpp")
target_include_directories(html-resource-browser-probe PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}" "${root}/src" "${root}/src/share")
target_link_libraries(html-resource-browser-probe PRIVATE
  Qt6::Widgets Qt6::WebEngineCore Qt6::Core5Compat)
target_compile_features(html-resource-browser-probe PRIVATE cxx_std_17)
file(SHA256 "${root}/src/database/WizDatabase.cpp" database_sha)
file(SHA256 "${root}/src/share/WizMarkdownExportTransaction.cpp" product_transaction_sha)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${root}/src/share/WizMarkdownExportTransaction.cpp")
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/transaction-prototype.h" transaction_sha)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/transaction-prototype.h")
add_executable(markdown-resource-probe resource-main.cpp)
target_include_directories(markdown-resource-probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${root}/src/share")
target_link_libraries(markdown-resource-probe PRIVATE Qt6::Core Qt6::Gui Qt6::Core5Compat)
target_compile_features(markdown-resource-probe PRIVATE cxx_std_17)
target_compile_definitions(markdown-resource-probe PRIVATE
  DATABASE_SOURCE_SHA="${database_sha}" WRITER_SOURCE_SHA="${writer_sha}" NAMES_SOURCE_SHA="${names_sha}"
  TRANSACTION_SOURCE_SHA="${transaction_sha}")
if(WIZ_MARKDOWN_EXPORT_SANITIZE)
  target_compile_options(markdown-resource-probe PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(markdown-resource-probe PRIVATE -fsanitize=address,undefined)
endif()

# Compile the product transaction helper itself against the same extracted
# database/export path and synthetic archive adapter.
add_executable(markdown-product-transaction
  product-transaction-main.cpp
  "${root}/src/share/WizMarkdownExportTransaction.cpp")
target_include_directories(markdown-product-transaction PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}" "${root}/src" "${root}/src/share")
target_link_libraries(markdown-product-transaction PRIVATE Qt6::Core Qt6::Gui Qt6::Core5Compat)
target_compile_features(markdown-product-transaction PRIVATE cxx_std_17)
target_compile_definitions(markdown-product-transaction PRIVATE
  WIZNOTE_HTML_EXPORT_TRANSACTION_TESTING=1
  DATABASE_SOURCE_SHA="${database_sha}"
  PRODUCT_TRANSACTION_SOURCE_SHA="${product_transaction_sha}")
if(WIZ_MARKDOWN_EXPORT_SANITIZE)
  target_compile_options(markdown-product-transaction PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(markdown-product-transaction PRIVATE -fsanitize=address,undefined)
endif()

# Test-only restart recovery, persistence-barrier and destination-lock protocol.
# It uses only synthetic QTemporaryDir state and is never linked into the product.
add_executable(markdown-recovery-prototype recovery-prototype-main.cpp)
target_link_libraries(markdown-recovery-prototype PRIVATE Qt6::Core)
target_compile_features(markdown-recovery-prototype PRIVATE cxx_std_17)
if(WIZ_MARKDOWN_EXPORT_SANITIZE)
  target_compile_options(markdown-recovery-prototype PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(markdown-recovery-prototype PRIVATE -fsanitize=address,undefined)
endif()

# Same actual ordinary transaction corpus with the experimental fresh-target
# reservation compiled ON. Still synthetic data, no DB/account/runtime feature.
add_executable(markdown-recovery-gated-transaction product-transaction-main.cpp
  "${root}/src/share/WizMarkdownExportTransaction.cpp"
  "${root}/src/share/WizMarkdownExportRecovery.cpp")
target_include_directories(markdown-recovery-gated-transaction PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}" "${root}/src" "${root}/src/share")
target_link_libraries(markdown-recovery-gated-transaction PRIVATE Qt6::Core Qt6::Gui Qt6::Core5Compat)
target_compile_features(markdown-recovery-gated-transaction PRIVATE cxx_std_17)
target_compile_definitions(markdown-recovery-gated-transaction PRIVATE
  WIZNOTE_HTML_EXPORT_TRANSACTION_TESTING=1
  WIZNOTE_ENABLE_MARKDOWN_RECOVERY=1 DATABASE_SOURCE_SHA="${database_sha}"
  PRODUCT_TRANSACTION_SOURCE_SHA="${product_transaction_sha}")
if(WIZ_MARKDOWN_EXPORT_SANITIZE)
  target_compile_options(markdown-recovery-gated-transaction PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(markdown-recovery-gated-transaction PRIVATE -fsanitize=address,undefined)
endif()

# Compile the dormant product recovery core directly, without linking it into
# the App or changing the current export runtime path. Fault checkpoints exist
# only in this standalone target.
add_library(markdown-recovery-audit-production-compile OBJECT
  "${root}/src/share/WizMarkdownExportRecoveryAudit.cpp")
target_include_directories(markdown-recovery-audit-production-compile PRIVATE "${root}/src")
target_link_libraries(markdown-recovery-audit-production-compile PRIVATE Qt6::Core)
target_compile_features(markdown-recovery-audit-production-compile PRIVATE cxx_std_17)

# One bounded publication/ordinary-failure/actual-child-interruption matrix.
# Fault knobs are compiled only here, not in the normal App or gated corpus.
add_executable(markdown-recovery-publication product-transaction-main.cpp
  "${root}/src/share/WizMarkdownExportTransaction.cpp"
  "${root}/src/share/WizMarkdownExportRecovery.cpp"
  "${root}/src/share/WizMarkdownExportRecoveryCoordinator.cpp"
  "${root}/src/share/WizMarkdownExportRecoveryAudit.cpp")
target_include_directories(markdown-recovery-publication PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}" "${root}/src" "${root}/src/share")
target_link_libraries(markdown-recovery-publication PRIVATE Qt6::Core Qt6::Gui Qt6::Core5Compat)
target_compile_features(markdown-recovery-publication PRIVATE cxx_std_17)
target_compile_definitions(markdown-recovery-publication PRIVATE
  WIZNOTE_ENABLE_MARKDOWN_RECOVERY=1 WIZ_VERIFY_PUBLICATION=1 WIZ_MARKDOWN_EXPORT_RECOVERY_TESTING=1
  DATABASE_SOURCE_SHA="${database_sha}" PRODUCT_TRANSACTION_SOURCE_SHA="${product_transaction_sha}")
if(WIZ_MARKDOWN_EXPORT_SANITIZE)
  target_compile_options(markdown-recovery-publication PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(markdown-recovery-publication PRIVATE -fsanitize=address,undefined)
endif()

add_library(markdown-recovery-coordinator-production-compile OBJECT
  "${root}/src/share/WizMarkdownExportRecoveryCoordinator.cpp"
  "${root}/src/share/WizMarkdownExportRecovery.cpp")
target_include_directories(markdown-recovery-coordinator-production-compile PRIVATE "${root}/src")
target_link_libraries(markdown-recovery-coordinator-production-compile PRIVATE Qt6::Core)
target_compile_features(markdown-recovery-coordinator-production-compile PRIVATE cxx_std_17)

file(SHA256 "${root}/src/share/WizMarkdownExportRecovery.cpp" recovery_core_sha)
file(SHA256 "${root}/src/share/WizMarkdownExportRecoveryAudit.cpp" recovery_audit_sha)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${root}/src/share/WizMarkdownExportRecovery.cpp"
  "${root}/src/share/WizMarkdownExportRecoveryAudit.cpp")
add_executable(markdown-recovery-core
  recovery-core-main.cpp
  "${root}/src/share/WizMarkdownExportRecoveryCoordinator.cpp"
  "${root}/src/share/WizMarkdownExportRecovery.cpp"
  "${root}/src/share/WizMarkdownExportRecoveryAudit.cpp")
target_include_directories(markdown-recovery-core PRIVATE "${root}/src")
target_link_libraries(markdown-recovery-core PRIVATE Qt6::Core)
target_compile_features(markdown-recovery-core PRIVATE cxx_std_17)
target_compile_definitions(markdown-recovery-core PRIVATE
  RECOVERY_CORE_SOURCE_SHA="${recovery_core_sha}"
  RECOVERY_AUDIT_SOURCE_SHA="${recovery_audit_sha}"
  WIZ_MARKDOWN_EXPORT_RECOVERY_TESTING=1)
if(WIZ_MARKDOWN_EXPORT_SANITIZE)
  target_compile_options(markdown-recovery-core PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(markdown-recovery-core PRIVATE -fsanitize=address,undefined)
endif()

# Actual dormant widget, but deliberately no recovery/audit/coordinator object
# or App/DB/WebEngine linkage. The harness forces Qt's offscreen platform.
find_package(Qt6 6.9 REQUIRED COMPONENTS Test)
set(recovery_prompt_sources
  "${root}/src/widgets/WizMarkdownExportRecoveryPrompt.h"
  "${root}/src/widgets/WizMarkdownExportRecoveryPrompt.cpp")
add_library(markdown-recovery-prompt-production-compile OBJECT ${recovery_prompt_sources})
set_target_properties(markdown-recovery-prompt-production-compile PROPERTIES AUTOMOC ON)
target_include_directories(markdown-recovery-prompt-production-compile PRIVATE "${root}/src")
target_link_libraries(markdown-recovery-prompt-production-compile PRIVATE Qt6::Widgets)
target_compile_features(markdown-recovery-prompt-production-compile PRIVATE cxx_std_17)

add_executable(markdown-recovery-prompt recovery-prompt-main.cpp ${recovery_prompt_sources})
set_target_properties(markdown-recovery-prompt PROPERTIES AUTOMOC ON)
target_include_directories(markdown-recovery-prompt PRIVATE "${root}/src")
target_link_libraries(markdown-recovery-prompt PRIVATE Qt6::Widgets Qt6::Test)
target_compile_features(markdown-recovery-prompt PRIVATE cxx_std_17)
if(WIZ_MARKDOWN_EXPORT_SANITIZE)
  target_compile_options(markdown-recovery-prompt PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(markdown-recovery-prompt PRIVATE -fsanitize=address,undefined)
endif()

# Dormant GUI-context/worker-start gate only: no executor, audit, GUI widget,
# database, WebEngine or application quit-policy integration.
add_library(markdown-recovery-request-production-compile OBJECT
  "${root}/src/share/WizMarkdownExportRecoveryRequest.cpp")
target_include_directories(markdown-recovery-request-production-compile PRIVATE "${root}/src")
target_link_libraries(markdown-recovery-request-production-compile PRIVATE Qt6::Core)
target_compile_features(markdown-recovery-request-production-compile PRIVATE cxx_std_17)

add_executable(markdown-recovery-request recovery-request-main.cpp
  "${root}/src/share/WizMarkdownExportRecoveryRequest.cpp")
target_include_directories(markdown-recovery-request PRIVATE "${root}/src")
target_link_libraries(markdown-recovery-request PRIVATE Qt6::Core)
target_compile_features(markdown-recovery-request PRIVATE cxx_std_17)
if(WIZ_MARKDOWN_EXPORT_SANITIZE)
  target_compile_options(markdown-recovery-request PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(markdown-recovery-request PRIVATE -fsanitize=address,undefined)
endif()

set(recovery_session_sources
  "${root}/src/widgets/WizMarkdownExportRecoveryApp.cpp"
  "${root}/src/widgets/WizMarkdownExportRecoverySession.h"
  "${root}/src/widgets/WizMarkdownExportRecoverySession.cpp")
add_library(markdown-recovery-session-production-compile OBJECT ${recovery_session_sources})
set_target_properties(markdown-recovery-session-production-compile PROPERTIES AUTOMOC ON)
target_include_directories(markdown-recovery-session-production-compile PRIVATE "${root}/src")
target_link_libraries(markdown-recovery-session-production-compile PRIVATE Qt6::Widgets)
target_compile_features(markdown-recovery-session-production-compile PRIVATE cxx_std_17)

# Direct integration of the actual dormant UI/request/coordinator and existing
# application-owned queues, with synthetic files only and no account/DB/WebEngine.
add_executable(markdown-recovery-session recovery-session-main.cpp
  ${recovery_session_sources} ${recovery_prompt_sources}
  "${root}/src/share/WizMarkdownExportRecoveryRequest.cpp"
  "${root}/src/share/WizMarkdownExportRecoveryCoordinator.cpp"
  "${root}/src/share/WizMarkdownExportRecovery.cpp"
  "${root}/src/share/WizMarkdownExportRecoveryAudit.cpp"
  "${root}/src/share/WizThreads.cpp" "${root}/src/share/WizThreads_p.h")
set_target_properties(markdown-recovery-session PROPERTIES AUTOMOC ON)
target_include_directories(markdown-recovery-session PRIVATE "${root}/src")
target_link_libraries(markdown-recovery-session PRIVATE Qt6::Widgets Qt6::Test Qt6::Core5Compat)
target_compile_features(markdown-recovery-session PRIVATE cxx_std_17)
target_compile_definitions(markdown-recovery-session PRIVATE WIZ_MARKDOWN_EXPORT_RECOVERY_TESTING=1)
if(WIZ_MARKDOWN_EXPORT_SANITIZE)
  target_compile_options(markdown-recovery-session PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(markdown-recovery-session PRIVATE -fsanitize=address,undefined)
endif()

# Actual MainWindow quit/filter/preparation methods, with synthetic document,
# saver, sync and cleanup collaborators. No real MainWindow/DB/network startup.
extract_between("${root}/src/WizMainWindow.cpp"
  "bool WizMainWindow::eventFilter(" "void WizMainWindow::cleanOnQuit()" quit_entry_body)
extract_between("${root}/src/WizMainWindow.cpp"
  "void WizMainWindow::on_actionExit_triggered()" "void WizMainWindow::on_actionClose_triggered()" quit_action_body)
set(quit_body "${quit_entry_body}\n${quit_action_body}")
string(REPLACE "WizMainWindow::" "QuitHarness::" quit_body "${quit_body}")
string(REPLACE "QMessageBox::critical" "QuitMessageBox::critical" quit_body "${quit_body}")
# The synthetic sync collaborator emits QThread's stock finished signal; the
# actual connection/wait/one-attempt algorithm is otherwise unchanged.
string(REPLACE "&WizKMSyncThread::fullSyncAttemptFinished" "&QThread::finished" quit_body "${quit_body}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/actual-quit-flow.inc" "${quit_body}")
file(SHA256 "${root}/src/WizMainWindow.cpp" quit_source_sha)
add_executable(markdown-recovery-quit recovery-session-main.cpp
  ${recovery_session_sources} ${recovery_prompt_sources}
  "${root}/src/share/WizMarkdownExportRecoveryRequest.cpp"
  "${root}/src/share/WizMarkdownExportRecoveryCoordinator.cpp"
  "${root}/src/share/WizMarkdownExportRecovery.cpp"
  "${root}/src/share/WizMarkdownExportRecoveryAudit.cpp"
  "${root}/src/share/WizThreads.cpp" "${root}/src/share/WizThreads_p.h")
set_target_properties(markdown-recovery-quit PROPERTIES AUTOMOC ON)
target_include_directories(markdown-recovery-quit PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${root}/src")
target_link_libraries(markdown-recovery-quit PRIVATE Qt6::Widgets Qt6::Test Qt6::Core5Compat)
target_compile_features(markdown-recovery-quit PRIVATE cxx_std_17)
target_compile_definitions(markdown-recovery-quit PRIVATE WIZ_VERIFY_QUIT=1
  WIZNOTE_ENABLE_MARKDOWN_RECOVERY=1 WIZ_MARKDOWN_EXPORT_RECOVERY_TESTING=1
  QUIT_SOURCE_SHA="${quit_source_sha}")
if(WIZ_MARKDOWN_EXPORT_SANITIZE)
  target_compile_options(markdown-recovery-quit PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(markdown-recovery-quit PRIVATE -fsanitize=address,undefined)
endif()

# Extract the same real WebView export method with the App switch ON, not a
# second handwritten implementation. Only DB extraction/result UI are adapters.
add_executable(markdown-recovery-app-export main.cpp
  ${recovery_session_sources} ${recovery_prompt_sources}
  "${root}/src/share/WizMarkdownExportTransaction.cpp"
  "${root}/src/share/WizMarkdownExportRecoveryRequest.cpp"
  "${root}/src/share/WizMarkdownExportRecoveryCoordinator.cpp"
  "${root}/src/share/WizMarkdownExportRecovery.cpp"
  "${root}/src/share/WizMarkdownExportRecoveryAudit.cpp"
  "${root}/src/share/WizThreads.cpp" "${root}/src/share/WizThreads_p.h")
set_target_properties(markdown-recovery-app-export PROPERTIES AUTOMOC ON)
target_include_directories(markdown-recovery-app-export PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}" "${root}/src" "${root}/src/share")
target_link_libraries(markdown-recovery-app-export PRIVATE Qt6::Widgets Qt6::WebEngineCore Qt6::Core5Compat)
target_compile_features(markdown-recovery-app-export PRIVATE cxx_std_17)
target_compile_definitions(markdown-recovery-app-export PRIVATE
  WIZNOTE_ENABLE_MARKDOWN_RECOVERY=1 WIZ_MARKDOWN_EXPORT_RECOVERY_TESTING=1
  EXPORT_ACTUAL_REPLY_FIRST=1 EXPORT_PRODUCT_TRANSACTION=1 EXPORT_MODE="recovery"
  EXPORT_SOURCE_SHA="${export_sha}" WRITER_SOURCE_SHA="${writer_sha}" NAMES_SOURCE_SHA="${names_sha}"
  CANDIDATE_SOURCE_SHA="${candidate_sha}" LEGACY_SOURCE_SHA="${legacy_sha}")
if(WIZ_MARKDOWN_EXPORT_SANITIZE)
  target_compile_options(markdown-recovery-app-export PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(markdown-recovery-app-export PRIVATE -fsanitize=address,undefined)
endif()
