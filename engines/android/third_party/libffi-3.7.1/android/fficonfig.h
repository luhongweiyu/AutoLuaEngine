/* Android configuration for the vendored libffi 3.7.1 static build.
 *
 * This replaces the Autoconf-generated fficonfig.h. Architecture-specific
 * declarations still come from the upstream ffitarget.h selected by CMake.
 */
#ifndef XIAOYV_LIBFFI_ANDROID_FFICONFIG_H
#define XIAOYV_LIBFFI_ANDROID_FFICONFIG_H

#define EH_FRAME_FLAGS "a"
#define FFI_EXEC_STATIC_TRAMP 1
#define FFI_EXEC_TRAMPOLINE_TABLE 0
#define FFI_MMAP_EXEC_WRIT 1

#define HAVE_ALLOCA_H 1
#define HAVE_AS_CFI_PSEUDO_OP 1
#define HAVE_AS_X86_PCREL 1
#define HAVE_DLFCN_H 1
#define HAVE_HIDDEN_VISIBILITY_ATTRIBUTE 1
#define HAVE_INTTYPES_H 1
#define HAVE_MEMCPY 1
#define HAVE_RO_EH_FRAME 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define STDC_HEADERS 1

#if defined(__SIZEOF_INT128__)
#define HAVE_INT128 1
#endif

#if defined(__SIZEOF_LONG_DOUBLE__) && defined(__SIZEOF_DOUBLE__) \
        && __SIZEOF_LONG_DOUBLE__ > __SIZEOF_DOUBLE__
#define HAVE_LONG_DOUBLE 1
#endif

#define PACKAGE "libffi"
#define PACKAGE_BUGREPORT "http://github.com/libffi/libffi/issues"
#define PACKAGE_NAME "libffi"
#define PACKAGE_STRING "libffi 3.7.1"
#define PACKAGE_TARNAME "libffi"
#define PACKAGE_URL "https://github.com/libffi/libffi"
#define PACKAGE_VERSION "3.7.1"
#define VERSION "3.7.1"

#define SIZEOF_DOUBLE __SIZEOF_DOUBLE__
#define SIZEOF_LONG_DOUBLE __SIZEOF_LONG_DOUBLE__
#define SIZEOF_SIZE_T __SIZEOF_SIZE_T__

#ifdef LIBFFI_ASM
#define FFI_HIDDEN(name) .hidden name
#else
#define FFI_HIDDEN __attribute__((visibility("hidden")))
#endif

#endif /* XIAOYV_LIBFFI_ANDROID_FFICONFIG_H */
