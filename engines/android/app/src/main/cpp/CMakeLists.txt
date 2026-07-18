cmake_minimum_required(VERSION 3.22.1)

project(XiaoyvEngine)

set(LUA_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party/lua-5.4.8/src)
set(IMGUI_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party/imgui)

add_library(engine SHARED
        engine_jni.cpp
        core/api/color_api.cpp
        core/api/device_api.cpp
        core/api/font_api.cpp
        core/api/image_api.cpp
        core/api/imgui_api.cpp
        core/api/ime_api.cpp
        core/api/input_api.cpp
        core/api/ocr_api.cpp
        core/api/package_api.cpp
        core/api/runtime_api.cpp
        core/api/screen_api.cpp
        core/api/ui_api.cpp
        core/imgui_c_api.cpp
        core/system_c_api.cpp
        engine/engine.cpp
        engine/engine_command.cpp
        engine/json_value.cpp
        engine/script_task.cpp
        platform/android_bridge.cpp
        platform/imgui_renderer.cpp
        runtime/common/log_buffer.cpp
        runtime/lua/alpkg_package.cpp
        runtime/lua/host_api.cpp
        runtime/lua/imgui_lua_api.cpp
        runtime/lua/java_bridge.cpp
        runtime/lua/lua_runtime.cpp
        runtime/lua/lua_task_scheduler.cpp
        runtime/lua/lua_thread_api.cpp
        ${IMGUI_SRC_DIR}/imgui.cpp
        ${IMGUI_SRC_DIR}/imgui_draw.cpp
        ${IMGUI_SRC_DIR}/imgui_tables.cpp
        ${IMGUI_SRC_DIR}/imgui_widgets.cpp
        ${IMGUI_SRC_DIR}/backends/imgui_impl_android.cpp
        ${IMGUI_SRC_DIR}/backends/imgui_impl_opengl3.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../shared/native/image_core/image_core.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../shared/package/alpkg_crypto.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../shared/third_party/monocypher/monocypher.c
        ${LUA_SRC_DIR}/lapi.c
        ${LUA_SRC_DIR}/lauxlib.c
        ${LUA_SRC_DIR}/lbaselib.c
        ${LUA_SRC_DIR}/lcode.c
        ${LUA_SRC_DIR}/lcorolib.c
        ${LUA_SRC_DIR}/lctype.c
        ${LUA_SRC_DIR}/ldblib.c
        ${LUA_SRC_DIR}/ldebug.c
        ${LUA_SRC_DIR}/ldo.c
        ${LUA_SRC_DIR}/ldump.c
        ${LUA_SRC_DIR}/lfunc.c
        ${LUA_SRC_DIR}/lgc.c
        ${LUA_SRC_DIR}/linit.c
        ${LUA_SRC_DIR}/liolib.c
        ${LUA_SRC_DIR}/llex.c
        ${LUA_SRC_DIR}/lmathlib.c
        ${LUA_SRC_DIR}/lmem.c
        ${LUA_SRC_DIR}/loadlib.c
        ${LUA_SRC_DIR}/lobject.c
        ${LUA_SRC_DIR}/lopcodes.c
        ${LUA_SRC_DIR}/loslib.c
        ${LUA_SRC_DIR}/lparser.c
        ${LUA_SRC_DIR}/lstate.c
        ${LUA_SRC_DIR}/lstring.c
        ${LUA_SRC_DIR}/lstrlib.c
        ${LUA_SRC_DIR}/ltable.c
        ${LUA_SRC_DIR}/ltablib.c
        ${LUA_SRC_DIR}/ltm.c
        ${LUA_SRC_DIR}/lundump.c
        ${LUA_SRC_DIR}/lutf8lib.c
        ${LUA_SRC_DIR}/lvm.c
        ${LUA_SRC_DIR}/lzio.c)

target_include_directories(engine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../shared
        ${IMGUI_SRC_DIR}
        ${IMGUI_SRC_DIR}/backends
        ${LUA_SRC_DIR})

find_library(log-lib log)
find_library(android-lib android)
find_library(egl-lib EGL)
find_library(glesv3-lib GLESv3)
find_library(jnigraphics-lib jnigraphics)

target_link_libraries(engine
        ${log-lib}
        ${android-lib}
        ${egl-lib}
        ${glesv3-lib}
        ${jnigraphics-lib})
