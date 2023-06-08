cmake_minimum_required(VERSION 3.5)
project(simWS)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_MACOSX_RPATH 1)
set(CMAKE_CXX_STANDARD 17)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN=1)
endif()

set(USE_STANDALONE_ASIO ON CACHE BOOL "Use standalone 'asio' library instead of 'boost-asio'")

if(NOT COPPELIASIM_INCLUDE_DIR)
    if(DEFINED ENV{COPPELIASIM_ROOT_DIR})
        set(COPPELIASIM_INCLUDE_DIR $ENV{COPPELIASIM_ROOT_DIR}/programming/include)
    else()
        set(COPPELIASIM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../include)
    endif()
endif()
list(APPEND CMAKE_MODULE_PATH ${COPPELIASIM_INCLUDE_DIR}/cmake)
find_package(CoppeliaSim 4.5.1.7 REQUIRED)

include(FetchContent)

FetchContent_Declare(websocketpp
    URL https://github.com/zaphoyd/websocketpp/archive/refs/tags/0.8.2.tar.gz
)
FetchContent_GetProperties(websocketpp)
if(NOT websocketpp_POPULATED)
    FetchContent_Populate(websocketpp)
endif()

coppeliasim_generate_stubs(${CMAKE_CURRENT_BINARY_DIR}/generated XML_FILE ${CMAKE_CURRENT_SOURCE_DIR}/callbacks.xml LUA_FILE ${CMAKE_CURRENT_SOURCE_DIR}/simWS.lua)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

coppeliasim_add_plugin(simWS SOURCES plugin.cpp)
if(USE_STANDALONE_ASIO)
    target_compile_definitions(simWS PRIVATE -DASIO_STANDALONE)
endif()
target_include_directories(simWS PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/_deps/websocketpp-src)
