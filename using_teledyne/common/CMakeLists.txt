cmake_minimum_required(VERSION 2.8)
#file(GLOB_RECURSE SRCS "*.c" "*.h")
#add_library(common STATIC ${SRCS})
#set(GEV_INCLUDE "/usr/dalsa/GigeV/include")
#set(GIGEV_LIBS "/usr/dalsa/GigeV/lib")
#file(GLOB GLIBS "{GIGEV_LIBS}/*")
#set(GEV_OPT "/opt/genicam_v3_0/bin/Linux64_x64")
#set(GEV_OPT_INC "/opt/genicam_v3_0/library/CPP/include")

# The following line is very practical:
# it will allow you to automatically add the correct include directories with "target_link_libraries"
#add_library(common STATIC ${common_sources})
#target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${GEV_INCLUDE})
#${GLIBS} ${GEV_OPT} ${GEV_OPT_INC})

#include_directories(${CMAKE_CURRENT_SOURCE_DIR})
add_library(common INTERFACE)
target_include_directories(common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
