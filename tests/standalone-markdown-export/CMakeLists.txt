cmake_minimum_required(VERSION 3.24)
project(StandaloneMarkdownExport LANGUAGES CXX)
find_package(Qt6 6.9 REQUIRED COMPONENTS Widgets WebEngineCore)

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
string(REPLACE "WizDocumentWebView::" "ExportHarness::" export_body "${export_body}")
extract_between("${root}/src/share/WizMisc.cpp"
  "bool WizSaveUnicodeTextToUtf8File(const QString& strFileName, const QString& strText, bool addBom)"
  "bool WizSaveUnicodeTextToData(" writer_body)
string(REPLACE "bool WizSaveUnicodeTextToUtf8File(" "bool rawWriteUtf8UnderTest(" writer_body "${writer_body}")
extract_between("${root}/src/utils/WizMisc.cpp"
  "QString WizMisc::extractFileName(" "CString WizMisc::extractTitleTemplate(" names_body)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/actual-flow.inc"
  "${writer_body}\nnamespace Utils {\n${names_body}\n}\n${export_body}\n")
file(SHA256 "${root}/src/gui/documentviewer/WizDocumentWebView.cpp" export_sha)
file(SHA256 "${root}/src/share/WizMisc.cpp" writer_sha)
file(SHA256 "${root}/src/utils/WizMisc.cpp" names_sha)
add_executable(markdown-export-probe main.cpp)
target_include_directories(markdown-export-probe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(markdown-export-probe PRIVATE Qt6::Widgets Qt6::WebEngineCore)
target_compile_features(markdown-export-probe PRIVATE cxx_std_17)
target_compile_definitions(markdown-export-probe PRIVATE
  EXPORT_SOURCE_SHA="${export_sha}" WRITER_SOURCE_SHA="${writer_sha}" NAMES_SOURCE_SHA="${names_sha}")
