cmake_minimum_required(VERSION 3.5)
project(naquid C CXX)



# global compile options
set(DEBUG false CACHE BOOL "do debug build")
set(CHROMIUM_VERSION "63.0.3222.1")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -DHAVE_PTHREAD -DDISABLE_HISTOGRAM")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -DOPENSSL_NO_ASM -DOSATOMIC_USE_INLINED=1 -DPB_NO_PACKED_STRUCTS")
include_directories(src src/chromium src/chromium/third_party/icu/source/common ext)
include_directories(SYSTEM src/chromium/third_party/protobuf/src src/chromium/third_party/boringssl/src/include)
if (DEBUG)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -DDEBUG")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wall -DDEBUG")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2 -DNDEBUG")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2 -DNDEBUG")
endif()	



# srcs
set(lib_src
	./src/nq.cpp

	./src/basis/defs.cpp
	./src/basis/endian.cpp
	./src/basis/timespec.cpp

	./src/core/nq_alarm.cpp
	./src/core/nq_at_exit.cpp
	./src/core/nq_client.cpp
	./src/core/nq_client_loop.cpp
	./src/core/nq_client_session.cpp
	./src/core/nq_config.cpp
	./src/core/nq_dispatcher.cpp
	./src/core/nq_loop.cpp
	./src/core/nq_network_helper.cpp
	./src/core/nq_packet_reader.cpp
	./src/core/nq_proof_verifier.cpp
	./src/core/nq_server.cpp
	./src/core/nq_server_loop.cpp
	./src/core/nq_server_session.cpp
	./src/core/nq_session.cpp
	./src/core/nq_stream.cpp 
	./src/core/nq_worker.cpp

	./src/core/compat/nq_file_util.cpp
	./src/core/compat/nq_quic_socket_utils.cpp 
	./src/core/compat/nq_ref_counted.cpp
	./src/core/compat/nq_spdy_protocol.cpp 
	./src/core/compat/nq_string_util.cpp
	./src/core/compat/nq_url_canon_icu.cpp
)
set(lib_mm_src
	./src/core/compat/nq_foundation_util.mm
)
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/net.cmake)
# platforms
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/platform/common.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/platform/mac.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/platform/android.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/platform/linux.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/platform/win.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/platform/fuchsia.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/platform/posix.cmake)
# third party srcs
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/third_party/zlib.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/third_party/ssl.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/third_party/pb.cmake)
# define common sources
set(src ${lib_src} ${net_src} ${common_src} ${zlib_src} ${ssl_src} ${pb_src})



# target settings
if (IOS OR NQ_OSX_BUNDLE OR NQ_OSX)
	find_library(core_foundation CoreFoundation)
	find_library(cocoa Cocoa)
	find_library(iokit IOKit)
	find_library(security Security)
	set(nqsrc ${src} ${lib_mm_src} ${mac_src})
	if (IOS)
		# workaround for problem detailed in https://karp.id.au/a/2015/09/15/xcode-7s-new-linker-rules/
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -miphoneos-version-min=7.0 -Wno-nullability-completeness -D__ENABLE_KQUEUE__")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -miphoneos-version-min=7.0 -Wno-nullability-completeness")
		add_library(nq STATIC ${nqsrc})
		target_link_libraries(nq ${core_foundation} ${iokit} ${security})
	elseif (NQ_OSX_BUNDLE)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__ENABLE_KQUEUE__")
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -bundle")
		add_executable(nq MACOSX_BUNDLE ${nqsrc}) 	
		target_link_libraries(nq ${core_foundation} ${cocoa} ${iokit} ${security})
	elseif (NQ_OSX)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__ENABLE_KQUEUE__")
		add_library(nq STATIC ${nqsrc})
		target_link_libraries(nq ${core_foundation} ${cocoa} ${iokit} ${security})		
	endif()
elseif (ANDROID)
	set(nqsrc ${src} ${android_src})
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__ENABLE_EPOLL__")
	add_library(nq SHARED ${nqsrc})
	target_link_libraries(nq "-lc++_static -llog")
elseif (WINDOWS)
	# TODO(iyatomi): add windows build instruction
elseif (NQ_LINUX)
	set(nqsrc ${src} ${linux_src})
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__ENABLE_EPOLL__")
	add_library(nq SHARED ${nqsrc})
endif ()
