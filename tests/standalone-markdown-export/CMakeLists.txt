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
  target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${root}/src/share")
  target_link_libraries(${target} PRIVATE Qt6::Widgets Qt6::WebEngineCore)
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_compile_definitions(${target} PRIVATE
    EXPORT_SOURCE_SHA="${export_sha}" WRITER_SOURCE_SHA="${writer_sha}" NAMES_SOURCE_SHA="${names_sha}"
    EXPORT_MODE="${mode}" CANDIDATE_SOURCE_SHA="${candidate_sha}" LEGACY_SOURCE_SHA="${legacy_sha}")
  if(mode STREQUAL "candidate")
    target_compile_definitions(${target} PRIVATE EXPORT_CANDIDATE=1)
  elseif(mode STREQUAL "legacy")
    target_compile_definitions(${target} PRIVATE EXPORT_LEGACY=1)
  else()
    target_compile_definitions(${target} PRIVATE EXPORT_ACTUAL_REPLY_FIRST=1)
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
  "${copy_body}\n${reader_body}\n${writer_two_body}\nnamespace Utils {\n${names_body}\n${path_body}\n}\n${database_body}\n")
file(SHA256 "${root}/src/database/WizDatabase.cpp" database_sha)
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
