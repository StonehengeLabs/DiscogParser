cmake_minimum_required(VERSION 2.8.11)

project(DiscogParser)

find_package(Qt5Core)
find_package(Qt5Xml)
set(CMAKE_AUTOMOC ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP /wd4996")

###############################################################################
# Group \\
###############################################################################

set(DISCOGPARSER_HEADERS
)

set(DISCOGPARSER_SOURCES
   Main.cpp
)

source_group(\\ FILES
   ${DISCOGPARSER_HEADERS}
   ${DISCOGPARSER_SOURCES}
)

###############################################################################
# Executable
###############################################################################

add_executable(DiscogParser
   ${DISCOGPARSER_HEADERS}
   ${DISCOGPARSER_SOURCES}
)

target_link_libraries(DiscogParser
   Qt5::Core
   Qt5::Xml
)
