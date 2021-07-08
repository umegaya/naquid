cmake_minimum_required(VERSION 3.5)
project(naquid C CXX)



# global compile options
set(DEBUG false CACHE BOOL "do debug build")
set(BACKEND "" CACHE STRING "quic protocol implementation used. chromium(legacy)/quiche(default)")
set(CHROMIUM_VERSION "63.0.3222.1")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -DHAVE_PTHREAD -DDISABLE_HISTOGRAM")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -DOPENSSL_NO_ASM -DOSATOMIC_USE_INLINED=1 -DPB_NO_PACKED_STRUCTS")
include_directories(src ext ext/cares)
include_directories(SYSTEM ext/cares/src ext/json/include)
if (DEBUG)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -DDEBUG")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wall -DDEBUG")
else()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2 -DNDEBUG")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2 -DNDEBUG")
endif()	
if (BACKEND STREQUAL "chromium")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNQ_CHROMIUM_BACKEND")
	include_directories(src/backends/chromium src/chromium/backends/third_party/icu/source/common)
	include_directories(SYSTEM src/backends/chromium/third_party/protobuf/src src/backends/chromium/third_party/boringssl/src/include)
endif()


# srcs
set(lib_src
	./src/nq.cpp

	./src/basis/endian.cpp
	./src/basis/logger.cpp
	./src/basis/timespec.cpp

	./src/core/nq_alarm.cpp
	./src/core/nq_async_resolver.cpp
	./src/core/nq_at_exit.cpp
	./src/core/nq_boxer.cpp
	./src/core/nq_client.cpp
	./src/core/nq_client_loop.cpp
	./src/core/nq_client_session.cpp
	./src/core/nq_config.cpp
	./src/core/nq_dispatcher.cpp
	./src/core/nq_loop_base.cpp
	./src/core/nq_network_helper.cpp
	./src/core/nq_packet_reader.cpp
	./src/core/nq_packet_writer.cpp
	./src/core/nq_proof_verifier.cpp
	./src/core/nq_server.cpp
	./src/core/nq_server_loop.cpp
	./src/core/nq_server_session.cpp
	./src/core/nq_session.cpp
	./src/core/nq_stream.cpp 
	./src/core/nq_worker.cpp

	./src/core/compat/nq_loop.cpp
	./src/core/compat/nq_quic_alarm.cpp

	./src/core/platform/nq_jni_android.cpp
	./src/core/platform/nq_reachability.cpp
	./src/core/platform/nq_reachability_android.cpp
)
set(lib_mm_src
	./src/core/platform/nq_reachability_mac.mm
)
if (BACKEND STREQUAL "chromium")
set(compat_src
	./src/backends/chromium/stub/nq_file_util.cpp
	./src/backends/chromium/stub/nq_quic_socket_utils.cpp 
	./src/backends/chromium/stub/nq_ref_counted.cpp
	./src/backends/chromium/stub/nq_spdy_protocol.cpp 
	./src/backends/chromium/stub/nq_string_util.cpp
	./src/backends/chromium/stub/nq_url_canon_icu.cpp
)
set(compat_mm_src
	./src/backends/chromium/stub/nq_foundation_util.mm
)
else()
set(compats_src)
set(compat_mm_src)
endif()
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
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/third_party/cares.cmake)
# setup group specific compiler flags
set_source_files_properties(${cares_src} PROPERTIES COMPILE_FLAGS "-DHAVE_CONFIG_H -D_GNU_SOURCE")
# define common sources
set(src ${lib_src} ${compat_src} ${net_src} ${common_src} ${zlib_src} ${ssl_src} ${pb_src} ${cares_src})



# target settings
if (IOS OR NQ_OSX_BUNDLE OR NQ_OSX)
	find_library(core_foundation CoreFoundation)
	find_library(cocoa Cocoa)
	find_library(iokit IOKit)
	find_library(security Security)
	find_library(system_configuration SystemConfiguration)
	set(nqsrc ${src} ${lib_mm_src} ${compat_mm_src} ${mac_src})
	include_directories(ext/cares/config/darwin)
	if (IOS)
		# workaround for problem detailed in https://karp.id.au/a/2015/09/15/xcode-7s-new-linker-rules/
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -miphoneos-version-min=7.0 -Wno-nullability-completeness -D__ENABLE_KQUEUE__")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -miphoneos-version-min=7.0 -Wno-nullability-completeness")
		add_library(nq STATIC ${nqsrc})
		target_link_libraries(nq ${core_foundation} ${iokit} ${security} ${system_configuration})
	elseif (NQ_OSX_BUNDLE)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__ENABLE_KQUEUE__")
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -bundle")
		add_executable(nq MACOSX_BUNDLE ${nqsrc}) 	
		target_link_libraries(nq ${core_foundation} ${cocoa} ${iokit} ${security} ${system_configuration})
	elseif (NQ_OSX)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__ENABLE_KQUEUE__")
		add_library(nq STATIC ${nqsrc})
		target_link_libraries(nq ${core_foundation} ${cocoa} ${iokit} ${security} ${system_configuration})		
	endif()
elseif (ANDROID)
	include_directories(ext/cares/config/linux)
	set(nqsrc ${src} ${android_src})
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__ENABLE_EPOLL__")
	add_library(nq SHARED ${nqsrc})
	target_link_libraries(nq "-lc++_static -llog")
elseif (WINDOWS)
	messsage(FATAL_ERROR, "TODO(iyatomi): add windows build instruction")
elseif (NQ_LINUX)
	include_directories(ext/cares/config/linux)
	set(nqsrc ${src} ${linux_src})
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__ENABLE_EPOLL__")
	add_library(nq STATIC ${nqsrc})
endif ()
