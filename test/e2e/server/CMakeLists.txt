cmake_minimum_required(VERSION 3.0)
set(DEBUG true CACHE BOOL "do debug build")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -DHAVE_PTHREAD __ENABLE_KQUEUE__")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -DOPENSSL_NO_ASM")
include_directories(. ..)
include_directories(SYSTEM ../../../ext/libuv/include)
if (DEBUG)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -DDEBUG")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2")
endif()	

file(GLOB_RECURSE src [
	"./main.cpp" 
])

link_directories(../../../build/test)
add_executable(server ${src}) 	
target_link_libraries(server uv)
