cmake_minimum_required(VERSION 3.0)
set(DEBUG true CACHE BOOL "do debug build")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c14")
include_directories(. .. ./specs)
include_directories(SYSTEM ../../../src)
if (DEBUG)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -DDEBUG")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2")
endif()	

file(GLOB_RECURSE src [
	"./main.cpp" 
])

if (${TEST_OS} STREQUAL "osx")
	find_library(core_foundation CoreFoundation)
	find_library(cocoa Cocoa)
	find_library(iokit IOKit)
	find_library(security Security)
	set(platform_libs ${core_foundation} ${cocoa} ${iokit} ${security})
elseif(${TEST_OS} STREQUAL "linux")
	set(platform_libs pthread)
else()
	set(platform_libs "")
endif()
set(platform_lib_paths "../../../build/t/${TEST_OS}")

link_directories(${platform_lib_paths})

add_executable(server ${src})
target_link_libraries(server nq ${platform_libs})
