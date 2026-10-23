set(BUILD_EXAMPLES OFF CACHE BOOL "Do not build examples")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static linking")

# when including QuaZip sources directly into a project, QUAZIP_STATIC should
# be defined explicitly to avoid possible troubles with unnecessary importing/exporting.
if (WIN32)
    add_definitions(-DQUAZIP_STATIC)
endif()

add_subdirectory(framelesshelper)
# Retain the previous archive/filename behavior during the QuaZip update.
# No new compression dependency or configure-time dependency download.
set(QUAZIP_BZIP2 OFF CACHE BOOL "Keep existing zlib-only archive support")
set(QUAZIP_ENABLE_QTEXTCODEC ON CACHE BOOL "Keep existing filename encodings")
set(QUAZIP_FETCH_LIBS OFF CACHE BOOL "No automatic dependency downloads")
set(QUAZIP_FORCE_FETCH_LIBS OFF CACHE BOOL "No forced dependency downloads")
add_subdirectory(quazip)

# Build gumbo-parser
file(GLOB GUMBO_SOURCES gumbo-parser/src/*.c)
add_library(gumbo ${GUMBO_SOURCES})
if(WIN32)
	target_include_directories(gumbo PRIVATE "gumbo-parser/visualc/include")
endif()

#because gumbo is a pure C 99 library
set_property(TARGET gumbo PROPERTY C_STANDARD 99)
