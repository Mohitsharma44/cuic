cmake_minimum_required(VERSION 2.8)
#file(GLOB_RECURSE HEADERS "*.h")
#add_library(plog STATIC ${HEADERS})
add_library(plog INTERFACE)
# The following line is very practical:
# it will allow you to automatically add the correct include directories with "target_link_libraries"
#target_link_directories(plog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
#target_include_directories(plog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(plog INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
