cmake_minimum_required(VERSION 3.22)

# 文件用途：构建 Windows 侧 ALPKG 打包器；它与 Android 引擎共用同一份 Lua 5.4.8 源码和加密实现。
project(AutoLuaPack LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(REPOSITORY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(LUA_SRC_DIR ${REPOSITORY_ROOT}/engines/android/third_party/lua-5.4.8/src)
set(ENGINE_COMMAND_DIR ${REPOSITORY_ROOT}/engines/android/app/src/main/cpp/engine)

add_executable(autolua_pack
        src/pack_main.cpp
        ${REPOSITORY_ROOT}/shared/package/alpkg_crypto.c
        ${REPOSITORY_ROOT}/shared/third_party/monocypher/monocypher.c
        ${ENGINE_COMMAND_DIR}/json_value.cpp
        ${LUA_SRC_DIR}/lapi.c
        ${LUA_SRC_DIR}/lauxlib.c
        ${LUA_SRC_DIR}/lcode.c
        ${LUA_SRC_DIR}/lctype.c
        ${LUA_SRC_DIR}/ldebug.c
        ${LUA_SRC_DIR}/ldo.c
        ${LUA_SRC_DIR}/ldump.c
        ${LUA_SRC_DIR}/lfunc.c
        ${LUA_SRC_DIR}/lgc.c
        ${LUA_SRC_DIR}/llex.c
        ${LUA_SRC_DIR}/lmem.c
        ${LUA_SRC_DIR}/lobject.c
        ${LUA_SRC_DIR}/lopcodes.c
        ${LUA_SRC_DIR}/lparser.c
        ${LUA_SRC_DIR}/lstate.c
        ${LUA_SRC_DIR}/lstring.c
        ${LUA_SRC_DIR}/ltable.c
        ${LUA_SRC_DIR}/ltm.c
        ${LUA_SRC_DIR}/lundump.c
        ${LUA_SRC_DIR}/lvm.c
        ${LUA_SRC_DIR}/lzio.c
)

target_include_directories(autolua_pack PRIVATE
        ${REPOSITORY_ROOT}/shared
        ${REPOSITORY_ROOT}/shared/package
        ${ENGINE_COMMAND_DIR}
        ${LUA_SRC_DIR}
)

if (WIN32)
    target_link_libraries(autolua_pack PRIVATE bcrypt shell32)
endif()

if (MSVC)
    target_compile_options(autolua_pack PRIVATE /W4)
else()
    target_compile_options(autolua_pack PRIVATE -Wall -Wextra)
endif()
