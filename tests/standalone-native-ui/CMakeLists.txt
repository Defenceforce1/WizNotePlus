cmake_minimum_required(VERSION 3.24)
project(StandaloneNativeUi LANGUAGES CXX OBJCXX)
if(NOT APPLE)
  message(FATAL_ERROR "This explicitly foreground probe requires macOS")
endif()
find_package(Qt6 6.9 REQUIRED COMPONENTS Widgets)
set(root "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(prompt "${root}/src/widgets/WizMarkdownExportRecoveryPrompt.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${prompt}")
file(SHA256 "${prompt}" prompt_sha)
add_executable(native-ui-probe MACOSX_BUNDLE main.mm
  "${root}/src/widgets/WizMarkdownExportRecoveryPrompt.h" "${prompt}")
set_target_properties(native-ui-probe PROPERTIES AUTOMOC ON
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.defenceforce1.wiznoteplus.tests.native-ui"
  MACOSX_BUNDLE_BUNDLE_NAME "Wiz Synthetic Native UI Probe")
target_compile_features(native-ui-probe PRIVATE cxx_std_17)
target_include_directories(native-ui-probe PRIVATE "${root}/src")
target_compile_definitions(native-ui-probe PRIVATE WIZ_NATIVE_PROMPT_SHA="${prompt_sha}")
target_link_libraries(native-ui-probe PRIVATE Qt6::Widgets "-framework AppKit")
