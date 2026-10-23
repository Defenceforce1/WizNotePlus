cmake_minimum_required(VERSION 3.24)
project(WizJsonCppUpgrade LANGUAGES CXX)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Core5Compat)
set(root "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(WIZ_JSONCPP_OLD_DIR "" CACHE PATH "Isolated original amalgamation")
set(WIZ_JSONCPP_NEW_DIR "" CACHE PATH "Official candidate amalgamation")
set(WIZ_JSONCPP_SUPPORT_OBJECTS "" CACHE PATH "Existing non-JSON helper objects")
foreach(v old new)
  string(TOUPPER "${v}" upper)
  set(vendor "${WIZ_JSONCPP_${upper}_DIR}")
  if(NOT EXISTS "${vendor}/jsoncpp.cpp")
    message(FATAL_ERROR "Explicit isolated old/new amalgamation paths required")
  endif()
  # Compile unchanged production methods against each matching header. Copies
  # avoid quoted relative includes accidentally selecting the working tree JSON.
  set(overlay "${CMAKE_CURRENT_BINARY_DIR}/${v}-overlay")
  file(MAKE_DIRECTORY "${overlay}/share/jsoncpp/json")
  configure_file("${vendor}/json/json.h" "${overlay}/share/jsoncpp/json/json.h" COPYONLY)
  configure_file("${vendor}/json/json-forwards.h" "${overlay}/share/jsoncpp/json/json-forwards.h" COPYONLY)
  foreach(unit WizObject WizRequest)
    configure_file("${root}/src/share/${unit}.cpp" "${overlay}/${unit}.cpp" COPYONLY)
    configure_file("${root}/src/share/${unit}.h" "${overlay}/share/${unit}.h" COPYONLY)
  endforeach()
  add_executable(jsoncpp-wrapper-${v} wrapper.cpp "${vendor}/jsoncpp.cpp"
    "${overlay}/WizObject.cpp" "${overlay}/WizRequest.cpp"
    "${WIZ_JSONCPP_SUPPORT_OBJECTS}/share/WizMisc.cpp.o"
    "${WIZ_JSONCPP_SUPPORT_OBJECTS}/share/WizQtHelper.cpp.o")
  target_include_directories(jsoncpp-wrapper-${v} BEFORE PRIVATE
    "${overlay}" "${overlay}/share" "${vendor}" "${root}/src" "${root}/src/share")
  target_compile_features(jsoncpp-wrapper-${v} PRIVATE cxx_std_17)
  target_compile_options(jsoncpp-wrapper-${v} PRIVATE -ffunction-sections -fdata-sections)
  target_link_options(jsoncpp-wrapper-${v} PRIVATE -Wl,-dead_strip)
  target_link_libraries(jsoncpp-wrapper-${v} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Core5Compat)
endforeach()
