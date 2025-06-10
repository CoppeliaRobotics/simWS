cmake_minimum_required(VERSION 3.16.3)
project(simWS)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_MACOSX_RPATH 1)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN=1)
endif()

if(NOT COPPELIASIM_INCLUDE_DIR)
    if(DEFINED ENV{COPPELIASIM_ROOT_DIR})
        set(COPPELIASIM_INCLUDE_DIR $ENV{COPPELIASIM_ROOT_DIR}/programming/include)
    else()
        set(COPPELIASIM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../include)
    endif()
endif()
list(APPEND CMAKE_MODULE_PATH ${COPPELIASIM_INCLUDE_DIR}/cmake)
find_package(CoppeliaSim 4.5.1.7 REQUIRED)

set(Qt Qt5 CACHE STRING "Qt version to use (e.g. Qt5)")
set_property(CACHE Qt PROPERTY STRINGS Qt5 Qt6)  #
find_package(${Qt} COMPONENTS Core WebSockets REQUIRED)

coppeliasim_generate_stubs(${CMAKE_CURRENT_BINARY_DIR}/generated XML_FILE ${CMAKE_CURRENT_SOURCE_DIR}/sourceCode/callbacks.xml LUA_FILE ${CMAKE_CURRENT_SOURCE_DIR}/lua/simWS.lua)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/sourceCode/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

coppeliasim_add_plugin(simWS SOURCES sourceCode/plugin.cpp)
target_link_libraries(simWS PRIVATE Qt::Core Qt::WebSockets)
