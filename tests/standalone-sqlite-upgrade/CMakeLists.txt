cmake_minimum_required(VERSION 3.24)
project(WizSqliteUpgrade LANGUAGES C CXX)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Core5Compat)
set(CMAKE_AUTOMOC ON)
set(root "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(WIZ_SQLITE_OLD_DIR "" CACHE PATH "Isolated frozen original SQLite source")
set(WIZ_SQLITE_NEW_DIR "" CACHE PATH "Verified SQLite candidate source")
set(WIZ_SQLITE_SUPPORT_OBJECTS "" CACHE PATH "Existing compatible support objects, not SQLite/index objects")
foreach(v old new)
  string(TOUPPER "${v}" upper)
  if(NOT EXISTS "${WIZ_SQLITE_${upper}_DIR}/sqlite3.c")
    message(FATAL_ERROR "Explicit old/new SQLite source directories required")
  endif()
  add_library(sqlite_${v} STATIC "${WIZ_SQLITE_${upper}_DIR}/sqlite3.c")
  target_compile_definitions(sqlite_${v} PRIVATE SQLITE_THREADSAFE=1 SQLITE_DEFAULT_PAGE_SIZE=1024)
  add_executable(sqlite-wrapper-${v} wrapper.cpp
    "${root}/src/database/cppsqlite3.cpp"
    "${root}/src/database/WizIndexBase.cpp"
    "${root}/src/database/WizIndexBase.h"
    "${root}/src/database/WizThumbIndex.cpp"
    "${WIZ_SQLITE_SUPPORT_OBJECTS}/share/WizMisc.cpp.o"
    "${WIZ_SQLITE_SUPPORT_OBJECTS}/share/WizQtHelper.cpp.o"
    "${WIZ_SQLITE_SUPPORT_OBJECTS}/share/WizObject.cpp.o")
  target_include_directories(sqlite-wrapper-${v} PRIVATE "${root}/src")
  target_link_libraries(sqlite-wrapper-${v} PRIVATE sqlite_${v} Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Core5Compat)
  target_compile_features(sqlite-wrapper-${v} PRIVATE cxx_std_17)
  target_link_options(sqlite-wrapper-${v} PRIVATE -Wl,-dead_strip)
endforeach()
