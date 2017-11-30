cmake_minimum_required(VERSION 3.0)
set(DEBUG false CACHE BOOL "do debug build")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c14")
include_directories(SYSTEM ../../ext)
if (DEBUG)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -DDEBUG")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2")
endif()	

file(GLOB_RECURSE src [
	"./main.cpp" 
])

file(GLOB_RECURSE src2 [
	"./main2.cpp" 
])

add_executable(bench ${src})

add_executable(bench2 ${src2})
