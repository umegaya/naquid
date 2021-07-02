#pragma once

// borrow and remove something from chromium build/build-config.h
#if defined(ANDROID)
  #define OS_ANDROID 1
#elif defined(__APPLE__)
  // only include TargetConditions after testing ANDROID as some android builds
  // on mac don't have this header available and it's not needed unless the target
  // is really mac/ios.
  #include <TargetConditionals.h>
  #define OS_MACOSX 1
  #if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    #define OS_IOS 1
  #endif  // defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#elif defined(__linux__)
  #define OS_LINUX 1
  // include a system header to pull in features.h for glibc/uclibc macros.
  #include <unistd.h>
  #if defined(__GLIBC__) && !defined(__UCLIBC__)
    // we really are using glibc, not uClibc pretending to be glibc
    #define LIBC_GLIBC 1
  #endif
#elif defined(_WIN32)
  #define OS_WIN 1
#else
  #error Please add support for your platform in platform.h
#endif
// NOTE: Adding a new port? Please follow
// https://chromium.googlesource.com/chromium/src/+/master/docs/new_port_policy.md

// For access to standard BSD features, use OS_BSD instead of a
// more specific macro.
#if defined(OS_FREEBSD) || defined(OS_NETBSD) || defined(OS_OPENBSD)
  #define OS_BSD 1
#endif

// For access to standard POSIXish features, use OS_POSIX instead of a
// more specific macro.
#if defined(OS_ANDROID) || defined(OS_LINUX) || defined(OS_MACOSX)
  #define OS_POSIX 1
#endif

// Use tcmalloc
#if (defined(OS_WIN) || defined(OS_LINUX) || defined(OS_ANDROID)) && \
    !defined(NO_TCMALLOC)
#define USE_TCMALLOC 1
#endif

// Compiler detection.
#if defined(__GNUC__)
#define COMPILER_GCC 1
#elif defined(_MSC_VER)
#define COMPILER_MSVC 1
#else
#error Please add support for your compiler in build/build_config.h
#endif

// Processor architecture detection.  For more info on what's defined, see:
//   http://msdn.microsoft.com/en-us/library/b0084kay.aspx
//   http://www.agner.org/optimize/calling_conventions.pdf
//   or with gcc, run: "echo | gcc -E -dM -"
#if defined(_M_X64) || defined(__x86_64__)
  #define ARCH_CPU_X86_FAMILY 1
  #define ARCH_CPU_X86_64 1
  #define ARCH_CPU_64_BITS 1
  #define ARCH_CPU_LITTLE_ENDIAN 1
#elif defined(_M_IX86) || defined(__i386__)
  #define ARCH_CPU_X86_FAMILY 1
  #define ARCH_CPU_X86 1
  #define ARCH_CPU_32_BITS 1
  #define ARCH_CPU_LITTLE_ENDIAN 1
#elif defined(__s390x__)
  #define ARCH_CPU_S390_FAMILY 1
  #define ARCH_CPU_S390X 1
  #define ARCH_CPU_64_BITS 1
  #define ARCH_CPU_BIG_ENDIAN 1
#elif defined(__s390__)
  #define ARCH_CPU_S390_FAMILY 1
  #define ARCH_CPU_S390 1
  #define ARCH_CPU_31_BITS 1
  #define ARCH_CPU_BIG_ENDIAN 1
#elif (defined(__PPC64__) || defined(__PPC__)) && defined(__BIG_ENDIAN__)
  #define ARCH_CPU_PPC64_FAMILY 1
  #define ARCH_CPU_PPC64 1
  #define ARCH_CPU_64_BITS 1
  #define ARCH_CPU_BIG_ENDIAN 1
#elif defined(__PPC64__)
  #define ARCH_CPU_PPC64_FAMILY 1
  #define ARCH_CPU_PPC64 1
  #define ARCH_CPU_64_BITS 1
  #define ARCH_CPU_LITTLE_ENDIAN 1
#elif defined(__ARMEL__)
  #define ARCH_CPU_ARM_FAMILY 1
  #define ARCH_CPU_ARMEL 1
  #define ARCH_CPU_32_BITS 1
  #define ARCH_CPU_LITTLE_ENDIAN 1
#elif defined(__aarch64__)
  #define ARCH_CPU_ARM_FAMILY 1
  #define ARCH_CPU_ARM64 1
  #define ARCH_CPU_64_BITS 1
  #define ARCH_CPU_LITTLE_ENDIAN 1
#elif defined(__pnacl__)
  #define ARCH_CPU_32_BITS 1
  #define ARCH_CPU_LITTLE_ENDIAN 1
#elif defined(__MIPSEL__)
  #if defined(__LP64__)
    #define ARCH_CPU_MIPS_FAMILY 1
    #define ARCH_CPU_MIPS64EL 1
    #define ARCH_CPU_64_BITS 1
    #define ARCH_CPU_LITTLE_ENDIAN 1
  #else
    #define ARCH_CPU_MIPS_FAMILY 1
    #define ARCH_CPU_MIPSEL 1
    #define ARCH_CPU_32_BITS 1
    #define ARCH_CPU_LITTLE_ENDIAN 1
  #endif
#elif defined(__MIPSEB__)
  #if defined(__LP64__)
    #define ARCH_CPU_MIPS_FAMILY 1
    #define ARCH_CPU_MIPS64 1
    #define ARCH_CPU_64_BITS 1
    #define ARCH_CPU_BIG_ENDIAN 1
  #else
    #define ARCH_CPU_MIPS_FAMILY 1
    #define ARCH_CPU_MIPS 1
    #define ARCH_CPU_32_BITS 1
    #define ARCH_CPU_BIG_ENDIAN 1
  #endif
#else
  #error Please add support for your architecture in platform.h
#endif

// Type detection for wchar_t.
#if defined(OS_WIN)
  #define WCHAR_T_IS_UTF16
#elif defined(OS_POSIX) && defined(COMPILER_GCC) && defined(__WCHAR_MAX__) && \
    (__WCHAR_MAX__ == 0x7fffffff || __WCHAR_MAX__ == 0xffffffff)
  #define WCHAR_T_IS_UTF32
#elif defined(OS_POSIX) && defined(COMPILER_GCC) && defined(__WCHAR_MAX__) && \
    (__WCHAR_MAX__ == 0x7fff || __WCHAR_MAX__ == 0xffff)
  // On Posix, we'll detect short wchar_t, but projects aren't guaranteed to
  // compile in this mode (in particular, Chrome doesn't). This is intended for
  // other projects using base who manage their own dependencies and make sure
  // short wchar works for them.
  #define WCHAR_T_IS_UTF16
#else
  #error Please add support for your compiler in platform.h
#endif

#if defined(OS_ANDROID)
  // The compiler thinks std::string::const_iterator and "const char*" are
  // equivalent types.
  #define STD_STRING_ITERATOR_IS_CHAR_POINTER
  // The compiler thinks base::string16::const_iterator and "char16*" are
  // equivalent types.
  #define BASE_STRING16_ITERATOR_IS_CHAR16_POINTER
#endif
