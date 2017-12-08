cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
project ("CUIC")
option(WITH_SYSTEM_LIBEVENT "Don't build libevent, use installed" ON)

# Versioning
set (CUIC_VERSION_MAJOR 1)
set (CUIC_VERSION_MINOR 0)

# CMake Modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake/Modules")
add_definitions("-std=c++11")

# Boost and ASIO stuff
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)
find_package(Boost 1.55 REQUIRED COMPONENTS system)

if(NOT Boost_FOUND)
  message( FATAL_ERROR "boost must be installed")
endif()

include_directories(${Boost_INCLUDE_DIRS})
add_library(asio_handler asiohandler.cpp asiohandler.h)
target_link_libraries(asio_handler ${Boost_LIBRARIES})

# AMQP-CPP
include(ExternalProject)
set(AMQPCPP_ROOT "${CMAKE_CURRENT_BINARY_DIR}/amqpcpp")
ExternalProject_Add(libamqpcpp
  URL "https://github.com/CopernicaMarketingSoftware/AMQP-CPP/archive/v2.6.2.tar.gz"
  URL_MD5 "24316FB610FE2F9B3D1C2EC4CF9001DD"
  CMAKE_ARGS -Wno-dev "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
  INSTALL_DIR ${AMQPCPP_ROOT}
  )
set(AMQPCPP_INCLUDE_DIR ${AMQPCPP_ROOT}/include)
set(AMQPCPP_LIBRARIES ${AMQPCPP_ROOT}/lib/libamqp-cpp.a)
message(STATUS "Found external dependency AMQP-CPP: " ${AMQPCPP_INCLUDE_DIR})
include_directories(${AMQPCPP_INCLUDE_DIR})
list(APPEND LIBRARIES ${AMQPCPP_LIBRARIES})
list(APPEND DEPENDENCIES libamqpcpp)


# Libevent
if (WITH_SYSTEM_LIBEVENT)
  find_package(Event REQUIRED)
  add_custom_target(libevent DEPENDS ${Event_INCLUDE_DIR}/event2/event.h)
else()
  set(Event_ROOT "${CMAKE_CURRENT_BINARY_DIR}/libevent")
  ExternalProject_Add(libevent
    URL "https://github.com/libevent/libevent/archive/release-2.0.22-stable.tar.gz"
    URL_MD5 "8913ef56ec329f2c046007bd634c7201"
    PATCH_COMMAND <SOURCE_DIR>/autogen.sh
    CONFIGURE_COMMAND <SOURCE_DIR>/configure --disable-shared --enable-static
    --disable-openssl --disable-libevent-regress --prefix <INSTALL_DIR>
    INSTALL_DIR ${Event_ROOT}
    )
  set(Event_INCLUDE_DIR ${Event_ROOT}/include)
  set(Event_LIBRARIES ${Event_ROOT}/lib/libevent_core.a)
  message(STATUS "Found external dependency Event: " ${Event_INCLUDE_DIR})
endif()
include_directories(${Event_INCLUDE_DIR})
list(APPEND LIBRARIES ${Event_LIBRARIES})
list(APPEND DEPENDENCIES libevent)

# threads
find_package(Threads)
list(APPEND LIBRARIES ${CMAKE_THREAD_LIBS_INIT})

# GigeV packages
include_directories("/usr/local/lib")
include_directories("/usr/dalsa/GigeV/include")
include_directories("/opt/genicam_v3_0/bin/Linux64_x64/")
include_directories("/opt/genicam_v3_0/library/CPP/include")

set(GIGEV_LIBS "/usr/dalsa/GigeV/include/")
file(GLOB GLIBS "{GIGEV_LIBS}/*.h")
list(APPEND LIBRARIES ${GLIBS})

find_library(GEVAPILIB NAMES GevApi HINTS "/usr/local/lib")
find_library(Xext NAMES Xext HINTS "/usr/local/lib")
find_library(X11 NAMES X11 HINTS "/usr/local/lib")
list(APPEND LIBRARIES ${Xext} ${X11} ${GEVAPILIB})

file(GLOB GAPIS "/opt/genicam_v3_0/bin/Linux64_x64/*.so")
list(APPEND LIBRARIES ${GAPIS})

# add executable
add_executable (genicam_cpp_demo genicam_cpp_demo.cpp ${GEVAPILIB} ${Xext} ${X11} ${GAPIS} )
target_link_libraries(genicam_cpp_demo ${LIBRARIES} asio_handler common plog)
add_dependencies(genicam_cpp_demo ${DEPENDENCIES})
