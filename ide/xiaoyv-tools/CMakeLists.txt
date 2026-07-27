cmake_minimum_required(VERSION 3.24)

project(XiaoyvTools VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(XIAOYV_BUILD_TESTS "Build Xiaoyv automated tests" OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network Concurrent Qml)
if(XIAOYV_BUILD_TESTS)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
endif()

set(REPOSITORY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(LUA_SOURCE_DIR ${REPOSITORY_ROOT}/engines/android/third_party/lua-5.4.8/src)

add_library(xiaoyv_image_core STATIC
        ${REPOSITORY_ROOT}/shared/native/image_core/image_core.cpp)
target_include_directories(xiaoyv_image_core PUBLIC ${REPOSITORY_ROOT}/shared)
if(MSVC)
    target_compile_options(xiaoyv_image_core PRIVATE /W4)
endif()

set(LUA_SOURCES
        ${LUA_SOURCE_DIR}/lapi.c
        ${LUA_SOURCE_DIR}/lauxlib.c
        ${LUA_SOURCE_DIR}/lbaselib.c
        ${LUA_SOURCE_DIR}/lcode.c
        ${LUA_SOURCE_DIR}/lcorolib.c
        ${LUA_SOURCE_DIR}/lctype.c
        ${LUA_SOURCE_DIR}/ldblib.c
        ${LUA_SOURCE_DIR}/ldebug.c
        ${LUA_SOURCE_DIR}/ldo.c
        ${LUA_SOURCE_DIR}/ldump.c
        ${LUA_SOURCE_DIR}/lfunc.c
        ${LUA_SOURCE_DIR}/lgc.c
        ${LUA_SOURCE_DIR}/linit.c
        ${LUA_SOURCE_DIR}/liolib.c
        ${LUA_SOURCE_DIR}/llex.c
        ${LUA_SOURCE_DIR}/lmathlib.c
        ${LUA_SOURCE_DIR}/lmem.c
        ${LUA_SOURCE_DIR}/loadlib.c
        ${LUA_SOURCE_DIR}/lobject.c
        ${LUA_SOURCE_DIR}/lopcodes.c
        ${LUA_SOURCE_DIR}/loslib.c
        ${LUA_SOURCE_DIR}/lparser.c
        ${LUA_SOURCE_DIR}/lstate.c
        ${LUA_SOURCE_DIR}/lstring.c
        ${LUA_SOURCE_DIR}/lstrlib.c
        ${LUA_SOURCE_DIR}/ltable.c
        ${LUA_SOURCE_DIR}/ltablib.c
        ${LUA_SOURCE_DIR}/ltm.c
        ${LUA_SOURCE_DIR}/lundump.c
        ${LUA_SOURCE_DIR}/lutf8lib.c
        ${LUA_SOURCE_DIR}/lvm.c
        ${LUA_SOURCE_DIR}/lzio.c)
add_library(xiaoyv_lua STATIC ${LUA_SOURCES})
target_include_directories(xiaoyv_lua PUBLIC ${LUA_SOURCE_DIR})

set(XIAOYV_TOOLS_SOURCES
        src/main_window.cpp
        src/main_window.h
        src/workspace/image_workspace.cpp
        src/workspace/image_workspace.h
        src/core/image_processing.cpp
        src/core/image_processing.h
        src/core/selection_range.h
        src/model/color_point_model.cpp
        src/model/color_point_model.h
        src/model/image_document.cpp
        src/model/image_document.h
        src/model/font_dictionary_document.cpp
        src/model/font_dictionary_document.h
        src/canvas/image_canvas.cpp
        src/canvas/image_canvas.h
        src/canvas/image_editor.cpp
        src/canvas/image_editor.h
        src/capture/desktop_capture.cpp
        src/capture/desktop_capture.h
        src/device/device_client.cpp
        src/device/device_client.h
        src/device/device_transport.cpp
        src/device/device_transport.h
        src/dialogs/connection_dialog.cpp
        src/dialogs/connection_dialog.h
        src/generator/generator_engine.cpp
        src/generator/generator_engine.h
        src/panels/color_panel.cpp
        src/panels/color_panel.h
        src/panels/analysis_panel.cpp
        src/panels/analysis_panel.h
        src/panels/font_panel.cpp
        src/panels/font_panel.h
        src/panels/font_dictionary_editor.cpp
        src/panels/font_dictionary_editor.h
        src/panels/script_editor_panel.cpp
        src/panels/script_editor_panel.h
        src/ui/action_catalog.h
        src/ui/app_theme.cpp
        src/ui/app_theme.h
        src/ui/compact_dock_widget.cpp
        src/ui/compact_dock_widget.h
        src/ui/instant_tooltip_filter.h
        src/ui/pixel_grid.cpp
        src/ui/pixel_grid.h
        src/ui/range_editor.cpp
        src/ui/range_editor.h
        src/ui/tool_icons.cpp
        src/ui/tool_icons.h)

add_library(xiaoyv_tools_lib STATIC ${XIAOYV_TOOLS_SOURCES})
qt_add_resources(xiaoyv_tools_lib "xiaoyv_branding"
        PREFIX "/branding"
        BASE resources
        FILES resources/xiaoyv_app_icon.png)
target_include_directories(xiaoyv_tools_lib PUBLIC src)
if(MSVC)
    target_compile_options(xiaoyv_tools_lib PRIVATE /W4)
endif()
target_link_libraries(xiaoyv_tools_lib PUBLIC
        xiaoyv_image_core
        xiaoyv_lua
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::Concurrent
        Qt6::Qml)
if(WIN32)
    target_link_libraries(xiaoyv_tools_lib PUBLIC user32 gdi32 dwmapi)
endif()

qt_add_executable(xiaoyv_tools src/main.cpp)
target_link_libraries(xiaoyv_tools PRIVATE xiaoyv_tools_lib)
if(WIN32)
    find_program(
            POWERSHELL_EXECUTABLE
            NAMES powershell.exe powershell
            REQUIRED)
    set(XIAOYV_WINDOWS_RESOURCE_DIR
            ${CMAKE_CURRENT_BINARY_DIR}/generated/windows)
    set(XIAOYV_WINDOWS_ICON_PATH
            ${XIAOYV_WINDOWS_RESOURCE_DIR}/xiaoyv_tools.ico)
    set(XIAOYV_WINDOWS_RESOURCE_PATH
            ${XIAOYV_WINDOWS_RESOURCE_DIR}/xiaoyv_tools.rc)
    file(MAKE_DIRECTORY ${XIAOYV_WINDOWS_RESOURCE_DIR})
    configure_file(
            resources/xiaoyv_tools.rc.in
            ${XIAOYV_WINDOWS_RESOURCE_PATH}
            @ONLY)
    add_custom_command(
            OUTPUT ${XIAOYV_WINDOWS_ICON_PATH}
            COMMAND ${POWERSHELL_EXECUTABLE}
                    -NoProfile
                    -ExecutionPolicy Bypass
                    -File ${CMAKE_CURRENT_SOURCE_DIR}/resources/generate_windows_icon.ps1
                    -SourcePath ${CMAKE_CURRENT_SOURCE_DIR}/resources/xiaoyv_app_icon.png
                    -OutputPath ${XIAOYV_WINDOWS_ICON_PATH}
            DEPENDS
                    resources/generate_windows_icon.ps1
                    resources/xiaoyv_app_icon.png
            COMMENT "生成小鱼 Windows EXE 图标"
            VERBATIM)
    add_custom_target(
            xiaoyv_windows_icon_resource
            DEPENDS ${XIAOYV_WINDOWS_ICON_PATH})
    add_dependencies(xiaoyv_tools xiaoyv_windows_icon_resource)
    set_property(
            SOURCE ${XIAOYV_WINDOWS_RESOURCE_PATH}
            APPEND PROPERTY OBJECT_DEPENDS ${XIAOYV_WINDOWS_ICON_PATH})
    target_sources(xiaoyv_tools PRIVATE ${XIAOYV_WINDOWS_RESOURCE_PATH})
endif()
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_target_properties(xiaoyv_tools PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

add_custom_target(xiaoyv_copy_formats ALL
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_CURRENT_BINARY_DIR}/formats
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/formats
                ${CMAKE_CURRENT_BINARY_DIR}/formats
        COMMENT "复制 Lua/JavaScript 取色格式")
add_dependencies(xiaoyv_tools xiaoyv_copy_formats)

if(XIAOYV_BUILD_TESTS)
    enable_testing()
    qt_add_executable(xiaoyv_model_tests tests/model_tests.cpp)
    target_link_libraries(xiaoyv_model_tests PRIVATE xiaoyv_tools_lib Qt6::Test)
    add_dependencies(xiaoyv_model_tests xiaoyv_copy_formats)

    qt_add_executable(xiaoyv_ui_tests tests/ui_tests.cpp)
    target_link_libraries(xiaoyv_ui_tests PRIVATE xiaoyv_tools_lib Qt6::Test)
    add_dependencies(xiaoyv_ui_tests xiaoyv_copy_formats)

    qt_add_executable(xiaoyv_app_smoke_tests tests/app_smoke_test.cpp)
    target_link_libraries(xiaoyv_app_smoke_tests PRIVATE xiaoyv_tools_lib Qt6::Test)
    add_dependencies(xiaoyv_app_smoke_tests xiaoyv_copy_formats)

    qt_add_executable(xiaoyv_device_tests tests/device_client_test.cpp)
    target_link_libraries(xiaoyv_device_tests PRIVATE xiaoyv_tools_lib Qt6::Test)
    add_dependencies(xiaoyv_device_tests xiaoyv_copy_formats)

    add_test(NAME xiaoyv_model_tests COMMAND xiaoyv_model_tests)
    add_test(NAME xiaoyv_ui_tests COMMAND xiaoyv_ui_tests)
    add_test(NAME xiaoyv_app_smoke_tests COMMAND xiaoyv_app_smoke_tests)
    add_test(NAME xiaoyv_device_tests COMMAND xiaoyv_device_tests)
    add_test(
            NAME xiaoyv_architecture_tests
            COMMAND ${CMAKE_COMMAND}
                    -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/architecture_rules.cmake)
endif()

if(MSVC)
    target_compile_options(xiaoyv_tools PRIVATE /W4)
    if(XIAOYV_BUILD_TESTS)
        foreach(target
                xiaoyv_model_tests
                xiaoyv_ui_tests
                xiaoyv_app_smoke_tests
                xiaoyv_device_tests)
            target_compile_options(${target} PRIVATE /W4)
        endforeach()
    endif()
endif()
