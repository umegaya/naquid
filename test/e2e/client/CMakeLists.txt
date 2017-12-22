cmake_minimum_required(VERSION 3.0)
set(DEBUG true CACHE BOOL "do debug build")
set(TEST_OS osx CACHE STRING "OS to test")
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
	"./specs/*.cpp"
	"../common.cpp"
])

file(GLOB_RECURSE bench_src [
	"./bench.cpp" 
])


set(platform_libs "")
if (${TEST_OS} STREQUAL "osx")
	find_library(core_foundation CoreFoundation)
	find_library(cocoa Cocoa)
	find_library(iokit IOKit)
	find_library(security Security)
	set(platform_libs ${core_foundation} ${cocoa} ${iokit} ${security})
endif()
set(platform_lib_paths "../../../build/t/${TEST_OS}")

message("plibpath: ${platform_lib_paths}")

link_directories(${platform_lib_paths})

add_executable(client ${src})
target_link_libraries(client nq ${platform_libs})

add_executable(bench ${bench_src})
target_link_libraries(bench nq ${platform_libs})
