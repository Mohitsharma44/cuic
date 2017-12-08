cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
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
add_definitions(-std=c++11)

set(PROGS
          rpc_client
          rpc_server
)


find_package(Threads)

find_library(AMQPCPP NAMES amqp-cpp HINTS "/usr/local/include")

foreach(item ${PROGS})
    add_executable(${item} "${item}.cpp")
    target_link_libraries(${item} ${AMQPCPP}
                                  asio_handler
                                  ${CMAKE_THREAD_LIBS_INIT})    
endforeach(item)

#All header files for amqcpp
set(amqpcpp_include "/usr/local/include/")
file(GLOB AMQCPP_HEADERS "${amqpcpp_include}*")
file(COPY ${AMQCPP_HEADERS}
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/amqpcpp)

include_directories(${PROJECT_SOURCE_DIR}
                    ${CMAKE_CURRENT_BINARY_DIR})
     
