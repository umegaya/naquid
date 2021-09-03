cmake_minimum_required(VERSION 3.5)
project(naquid C CXX)


# --------------------------------------
# global compile options
# --------------------------------------
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


# --------------------------------------
# core sources
# --------------------------------------
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
	./src/core/nq_client_base.cpp
	./src/core/nq_client_loop.cpp
	./src/core/nq_client_loop_base.cpp
	./src/core/nq_config.cpp
	./src/core/nq_config_base.cpp
	./src/core/nq_dispatcher_base.cpp
	./src/core/nq_dispatcher.cpp
	./src/core/nq_loop.cpp
	./src/core/nq_loop_base.cpp
	./src/core/nq_server.cpp
	./src/core/nq_server_loop.cpp
	./src/core/nq_server_session.cpp
	./src/core/nq_stream.cpp 
	./src/core/nq_worker.cpp

	./src/core/compat/nq_client_compat.cpp
	./src/core/compat/nq_client_loop_compat.cpp
	./src/core/compat/nq_config_compat.cpp
	./src/core/compat/nq_dispatcher_compat.cpp
	./src/core/compat/nq_loop_compat.cpp
	./src/core/compat/nq_quic_alarm.cpp
	./src/core/compat/nq_quic_types.cpp
	./src/core/compat/nq_server_session_compat.cpp
	./src/core/compat/nq_session.cpp
	./src/core/compat/nq_stream_compat.cpp
	./src/core/compat/nq_worker_compat.cpp

	./src/core/platform/nq_jni_android.cpp
	./src/core/platform/nq_reachability.cpp
	./src/core/platform/nq_reachability_android.cpp
)

# common objective C source (for iOS/OSX platform)
set(lib_mm_src .
	./src/core/platform/nq_reachability_mac.mm
)

# common third party srcs (initializes variable 'cares_src')
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/third_party/cares.cmake)
set_source_files_properties(${cares_src} PROPERTIES COMPILE_FLAGS "-DHAVE_CONFIG_H -D_GNU_SOURCE")

# define common sources
set(core_src ${lib_src} ${cares_src})


# --------------------------------------
# backend specific sources
# --------------------------------------
if (BACKEND STREQUAL "chromium")
	set(compat_lib_src
		./src/core/compat/chromium/nq_client_session.cpp
		./src/core/compat/chromium/nq_network_helper.cpp
		./src/core/compat/chromium/nq_packet_reader.cpp
		./src/core/compat/chromium/nq_packet_writer.cpp
		./src/core/compat/chromium/nq_proof_verifier.cpp
		./src/core/compat/chromium/nq_quic_client.cpp
		./src/core/compat/chromium/nq_quic_dispatcher.cpp

		./src/backends/chromium/stub/nq_file_util.cpp
		./src/backends/chromium/stub/nq_quic_socket_utils.cpp 
		./src/backends/chromium/stub/nq_ref_counted.cpp
		./src/backends/chromium/stub/nq_spdy_protocol.cpp 
		./src/backends/chromium/stub/nq_string_util.cpp
		./src/backends/chromium/stub/nq_url_canon_icu.cpp
	)
	set(compat_lib_mm_src
		./src/backends/chromium/stub/nq_foundation_util.mm
	)
	
	# common chromium sources
	include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/net.cmake)
	
	# common chromium third party sources
	include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/third_party/zlib.cmake)
	include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/third_party/ssl.cmake)
	include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/third_party/pb.cmake)

	# chromium platform sources (used on merging platform specific sources)
	include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/platform/common.cmake)
	include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/platform/mac.cmake)
	include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/platform/android.cmake)
	include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/platform/linux.cmake)
	include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/platform/win.cmake)
	include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/platform/fuchsia.cmake)
	include(${CMAKE_CURRENT_SOURCE_DIR}/tools/deps/platform/posix.cmake)

	# merge common chromium sources into core_src
	set(all_common_src ${core_src} ${compat_lib_src} ${net_src} ${common_src} ${zlib_src} ${ssl_src} ${pb_src})
endif()
if (BACKEND STREQUAL "quiche")
	# quiche does not have any compat source now
	set(compat_lib_src)
	set(compat_lib_mm_src)
	# quiche does not have any platform specific source now
	set(android_src)
	set(linux_src)
	set(mac_src)

	# merge common chromium sources into core_src
	set(all_common_src ${core_src} ${compat_lib_src})
endif()


# --------------------------------------
# platform specific sources
# --------------------------------------
if (IOS OR NQ_OSX_BUNDLE OR NQ_OSX)
	set(src ${all_common_src} ${lib_mm_src} ${compat_lib_mm_src} ${mac_src})
elseif (ANDROID)
	set(src ${all_common_src} ${android_src})
elseif (WINDOWS)
	message(FATAL_ERROR "TODO(iyatomi): add windows build instruction")
elseif (NQ_LINUX)
	set(src ${all_common_src} ${linux_src})
endif()


# --------------------------------------
# backend specific build settings
# --------------------------------------
if (BACKEND STREQUAL "chromium")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNQ_CHROMIUM_BACKEND")
	include_directories(src/backends/chromium src/chromium/backends/third_party/icu/source/common)
	include_directories(SYSTEM src/backends/chromium/third_party/protobuf/src src/backends/chromium/third_party/boringssl/src/include)
endif()
if (BACKEND STREQUAL "quiche")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNQ_QUICHE_BACKEND")
	include_directories(SYSTEM ext/quiche/include)
endif()


# --------------------------------------
# platform specific build settings
# --------------------------------------
if (IOS OR NQ_OSX_BUNDLE OR NQ_OSX)
	include_directories(ext/cares/config/darwin)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__ENABLE_KQUEUE__")
	if (NQ_OSX_BUNDLE)
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -bundle")
	endif()
elseif (WINDOWS)
	message(FATAL_ERROR "TODO(iyatomi): add windows build instruction")
else() # linux/android
	include_directories(ext/cares/config/linux)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__ENABLE_EPOLL__")
endif()


# --------------------------------------
# create build target
# --------------------------------------
if (IOS OR NQ_OSX_BUNDLE OR NQ_OSX)
	if (NQ_OSX_BUNDLE)
		add_executable(nq MACOSX_BUNDLE ${src})
	else()
		add_library(nq STATIC ${src})
	endif()
elseif (WINDOWS)
	message(FATAL_ERROR "TODO(iyatomi): add windows build instruction")
else() # linux/android
	add_library(nq STATIC ${src})
endif()


# --------------------------------------
# add backend specific dependencies
# --------------------------------------
if (BACKEND STREQUAL "chromium")
	set(backend_linked_targets)
endif()
if (BACKEND STREQUAL "quiche")
	if (DEBUG)
		set(rust_build_options cargo build)
		set(backend_linked_targets "${CMAKE_SOURCE_DIR}/ext/quiche/target/debug/deps/libquiche.a")
	else()
		set(rust_build_options cargo build --release)
		set(backend_linked_targets "${CMAKE_SOURCE_DIR}/ext/quiche/target/release/deps/libquiche.a")
	endif()
	include(ExternalProject)
	ExternalProject_Add(quiche 
		DOWNLOAD_COMMAND ""
		CONFIGURE_COMMAND ""
		BUILD_COMMAND cargo build COMMAND ${rust_build_options}
		BINARY_DIR "${CMAKE_SOURCE_DIR}/ext/quiche"
		INSTALL_COMMAND ""
		LOG_BUILD ON)
	add_dependencies(nq quiche)
endif()


# --------------------------------------
# add platform specific dependencies
# --------------------------------------
if (IOS OR NQ_OSX_BUNDLE OR NQ_OSX)
	if (IOS)
		target_link_libraries(nq ${backend_linked_targets})
	elseif (NQ_OSX_BUNDLE)
		target_link_libraries(nq ${backend_linked_targets})
	elseif (NQ_OSX)
		target_link_libraries(nq ${backend_linked_targets})
	endif()
elseif (ANDROID)
	target_link_libraries(nq "-lc++_static" "-llog" ${backend_linked_targets})
elseif (WINDOWS)
	message(FATAL_ERROR "TODO(iyatomi): add windows build instruction")
elseif (NQ_LINUX)
	target_link_libraries(nq ${backend_linked_targets})
endif()

