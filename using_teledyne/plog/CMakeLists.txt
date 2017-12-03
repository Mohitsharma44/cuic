cmake_minimum_required(VERSION 2.8)
add_library(plog INTERFACE)
target_include_directories(plog INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
