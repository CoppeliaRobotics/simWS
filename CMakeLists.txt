cmake_minimum_required(VERSION 3.16.3)
project(simWS)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_MACOSX_RPATH 1)
set(CMAKE_CXX_STANDARD 17)

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
find_package(OpenSSL REQUIRED)

include(FetchContent)

FetchContent_Declare(websocketpp
    URL https://github.com/zaphoyd/websocketpp/archive/refs/tags/0.8.2.tar.gz
)
FetchContent_GetProperties(websocketpp)
if(NOT websocketpp_POPULATED)
    FetchContent_Populate(websocketpp)
endif()

include(ExternalProject)

set(ASIO_INSTALL_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/asiolibs)
ExternalProject_Add(asio
    URL https://sourceforge.net/projects/asio/files/asio/1.24.0%20%28Stable%29/asio-1.24.0.tar.gz/download
    CONFIGURE_COMMAND ${CMAKE_CURRENT_BINARY_DIR}/asio-prefix/src/asio/configure
        --prefix=${ASIO_INSTALL_PREFIX}
    BUILD_COMMAND make
    INSTALL_COMMAND make install
)

coppeliasim_generate_stubs(${CMAKE_CURRENT_BINARY_DIR}/generated XML_FILE ${CMAKE_CURRENT_SOURCE_DIR}/sourceCode/callbacks.xml LUA_FILE ${CMAKE_CURRENT_SOURCE_DIR}/lua/simWS.lua)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/sourceCode/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

coppeliasim_add_plugin(simWS SOURCES sourceCode/plugin.cpp)
add_dependencies(simWS asio)
target_link_libraries(simWS PRIVATE OpenSSL::SSL OpenSSL::Crypto)
target_compile_definitions(simWS PRIVATE -DASIO_STANDALONE)
target_include_directories(simWS PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/_deps/websocketpp-src)
target_include_directories(simWS PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/asiolibs/include/)
