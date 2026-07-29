# 文件用途：把 cffi-lua 与 libffi 以静态库方式编入 libengine.so。
# 两个上游库都由 engines/android/third_party 下的 UPSTREAM.md 固定版本和来源。

if (NOT DEFINED LUA_SRC_DIR OR NOT DEFINED CFFI_LUA_SRC_DIR OR NOT DEFINED LIBFFI_SRC_DIR)
    message(FATAL_ERROR "CFFI 构建缺少 Lua、cffi-lua 或 libffi 源码目录。")
endif ()

if (NOT DEFINED CMAKE_ANDROID_ARCH_ABI)
    message(FATAL_ERROR "CFFI 仅支持 Android NDK 构建。")
endif ()

# libffi 的调用和回调入口由各 ABI 的 .S 文件提供。主工程默认只启用 C/C++，
# 因此在接入目标前显式启用汇编语言，确保这些源文件不会被 CMake 静默跳过。
enable_language(ASM)

set(XIAOYV_LIBFFI_ARCH_DIR "")
set(XIAOYV_LIBFFI_TARGET "")
set(XIAOYV_LIBFFI_HAVE_LONG_DOUBLE 0)

if (CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    set(XIAOYV_LIBFFI_ARCH_DIR "${LIBFFI_SRC_DIR}/src/aarch64")
    set(XIAOYV_LIBFFI_TARGET "AARCH64")
    set(XIAOYV_LIBFFI_HAVE_LONG_DOUBLE 1)
    set(XIAOYV_LIBFFI_ARCH_SOURCES
            "${LIBFFI_SRC_DIR}/src/aarch64/ffi.c"
            "${LIBFFI_SRC_DIR}/src/aarch64/sysv.S")
elseif (CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
    set(XIAOYV_LIBFFI_ARCH_DIR "${LIBFFI_SRC_DIR}/src/arm")
    set(XIAOYV_LIBFFI_TARGET "ARM")
    set(XIAOYV_LIBFFI_ARCH_SOURCES
            "${LIBFFI_SRC_DIR}/src/arm/ffi.c"
            "${LIBFFI_SRC_DIR}/src/arm/sysv.S")
elseif (CMAKE_ANDROID_ARCH_ABI STREQUAL "x86")
    set(XIAOYV_LIBFFI_ARCH_DIR "${LIBFFI_SRC_DIR}/src/x86")
    set(XIAOYV_LIBFFI_TARGET "X86")
    set(XIAOYV_LIBFFI_ARCH_SOURCES
            "${LIBFFI_SRC_DIR}/src/x86/ffi.c"
            "${LIBFFI_SRC_DIR}/src/x86/sysv.S")
elseif (CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
    set(XIAOYV_LIBFFI_ARCH_DIR "${LIBFFI_SRC_DIR}/src/x86")
    set(XIAOYV_LIBFFI_TARGET "X86_64")
    set(XIAOYV_LIBFFI_HAVE_LONG_DOUBLE 1)
    set(XIAOYV_LIBFFI_ARCH_SOURCES
            "${LIBFFI_SRC_DIR}/src/x86/ffi64.c"
            # ffi64.c 会保留 EFI/Win64 ABI 分支；即使 Android 默认走 Unix64，
            # 最终链接仍需这两个上游实现提供备用符号。
            "${LIBFFI_SRC_DIR}/src/x86/ffiw64.c"
            "${LIBFFI_SRC_DIR}/src/x86/unix64.S"
            "${LIBFFI_SRC_DIR}/src/x86/win64.S")
else ()
    message(FATAL_ERROR "CFFI 暂不支持 Android ABI: ${CMAKE_ANDROID_ARCH_ABI}")
endif ()

# libffi 的 ffi.h 由其 Autoconf 模板生成。这里在 CMake 配置期完成等价替换，
# 避免引入构建时运行 Autotools 的额外环境依赖。
set(XIAOYV_LIBFFI_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/libffi")
file(MAKE_DIRECTORY "${XIAOYV_LIBFFI_GENERATED_INCLUDE_DIR}")

set(_xiaoyv_libffi_restore_target FALSE)
if (DEFINED TARGET)
    set(_xiaoyv_libffi_restore_target TRUE)
    set(_xiaoyv_libffi_saved_target "${TARGET}")
endif ()
set(TARGET "${XIAOYV_LIBFFI_TARGET}")
set(HAVE_LONG_DOUBLE "${XIAOYV_LIBFFI_HAVE_LONG_DOUBLE}")
set(FFI_EXEC_TRAMPOLINE_TABLE 0)
set(FFI_VERSION_STRING "3.7.1")
set(FFI_VERSION_NUMBER 30701)
configure_file(
        "${LIBFFI_SRC_DIR}/include/ffi.h.in"
        "${XIAOYV_LIBFFI_GENERATED_INCLUDE_DIR}/ffi.h"
        @ONLY)
if (_xiaoyv_libffi_restore_target)
    set(TARGET "${_xiaoyv_libffi_saved_target}")
else ()
    unset(TARGET)
endif ()

set(XIAOYV_LIBFFI_INCLUDE_DIRS
        "${XIAOYV_LIBFFI_GENERATED_INCLUDE_DIR}"
        "${LIBFFI_SRC_DIR}/android"
        "${LIBFFI_SRC_DIR}/include"
        "${LIBFFI_SRC_DIR}/src"
        "${XIAOYV_LIBFFI_ARCH_DIR}")

add_library(xiaoyv_libffi STATIC
        "${LIBFFI_SRC_DIR}/src/closures.c"
        "${LIBFFI_SRC_DIR}/src/java_raw_api.c"
        "${LIBFFI_SRC_DIR}/src/prep_cif.c"
        "${LIBFFI_SRC_DIR}/src/raw_api.c"
        "${LIBFFI_SRC_DIR}/src/tramp.c"
        "${LIBFFI_SRC_DIR}/src/types.c"
        ${XIAOYV_LIBFFI_ARCH_SOURCES})
set_target_properties(xiaoyv_libffi PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        POSITION_INDEPENDENT_CODE ON)
target_include_directories(xiaoyv_libffi PRIVATE ${XIAOYV_LIBFFI_INCLUDE_DIRS})

add_library(xiaoyv_cffi_lua STATIC
        "${CFFI_LUA_SRC_DIR}/util.cc"
        "${CFFI_LUA_SRC_DIR}/ffilib.cc"
        "${CFFI_LUA_SRC_DIR}/parser.cc"
        "${CFFI_LUA_SRC_DIR}/ast.cc"
        "${CFFI_LUA_SRC_DIR}/lib.cc"
        "${CFFI_LUA_SRC_DIR}/ffi.cc"
        "${CFFI_LUA_SRC_DIR}/main.cc")
set_target_properties(xiaoyv_cffi_lua PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(xiaoyv_cffi_lua PRIVATE FFI_LITTLE_ENDIAN=1)
target_include_directories(xiaoyv_cffi_lua PRIVATE
        "${CFFI_LUA_SRC_DIR}"
        "${LUA_SRC_DIR}"
        ${XIAOYV_LIBFFI_INCLUDE_DIRS})
target_link_libraries(xiaoyv_cffi_lua PUBLIC xiaoyv_libffi ${dl-lib})
