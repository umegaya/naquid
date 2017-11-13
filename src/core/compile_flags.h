#if defined(BASE_DEBUG_COMPILE_FLAGS)
#define BUILDFLAG_INTERNAL_CAN_UNWIND_WITH_FRAME_POINTERS() 1
#define BUILDFLAG_INTERNAL_ENABLE_LOCATION_SOURCE() 1
#define BUILDFLAG_INTERNAL_ENABLE_PROFILING() 1
#endif

#if defined(BASE_ALLOCATOR_COMPILE_FLAGS)
#define BUILDFLAG_INTERNAL_USE_ALLOCATOR_SHIM() 1
#endif

#if defined(NET_COMPILE_FLAGS)
#define BUILDFLAG_INTERNAL_USE_BYTE_CERTS() 1
#define BUILDFLAG_INTERNAL_INCLUDE_TRANSPORT_SECURITY_STATE_PRELOAD_LIST() 0

#endif
