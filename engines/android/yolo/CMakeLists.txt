cmake_minimum_required(VERSION 3.22.1)

# 该工程只构建可选 libxiaoyv_yolo.so；Android App 的 CMake 工程绝不包含 NCNN。
project(XiaoyvYolo LANGUAGES C CXX)

set(XIAOYV_APP_CPP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp)
set(XIAOYV_NCNN_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/ncnn)
set(XIAOYV_YOLO_OUTPUT_DIR "" CACHE PATH "Directory that receives libxiaoyv_yolo.so")

if(NOT EXISTS ${XIAOYV_NCNN_SRC_DIR}/CMakeLists.txt)
    message(FATAL_ERROR "缺少官方 NCNN 源码：${XIAOYV_NCNN_SRC_DIR}")
endif()

# 第一阶段固定使用 CPU NCNN，并把它静态并入独立 yolo SO。没有 libncnn.so、没有 Vulkan
# shader，也不会影响不选择 YOLO 的 APK 体积。
set(NCNN_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(NCNN_VULKAN OFF CACHE BOOL "" FORCE)
set(NCNN_OPENMP OFF CACHE BOOL "" FORCE)
set(NCNN_C_API OFF CACHE BOOL "" FORCE)
set(NCNN_PLATFORM_API OFF CACHE BOOL "" FORCE)
set(NCNN_INSTALL_SDK OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${XIAOYV_NCNN_SRC_DIR} ${CMAKE_CURRENT_BINARY_DIR}/ncnn EXCLUDE_FROM_ALL)

add_library(xiaoyv_yolo SHARED
        ${XIAOYV_APP_CPP_DIR}/yolo/yolo_jni.cpp
        ${XIAOYV_APP_CPP_DIR}/yolo/yolo_runtime.cpp)
target_compile_features(xiaoyv_yolo PRIVATE cxx_std_17)
target_include_directories(xiaoyv_yolo PRIVATE
        ${XIAOYV_APP_CPP_DIR}
        ${XIAOYV_NCNN_SRC_DIR}/src)

find_library(log-lib log)
target_link_libraries(xiaoyv_yolo PRIVATE ncnn ${log-lib})

if(NOT XIAOYV_YOLO_OUTPUT_DIR STREQUAL "")
    set_target_properties(xiaoyv_yolo PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY ${XIAOYV_YOLO_OUTPUT_DIR}
            LIBRARY_OUTPUT_DIRECTORY_DEBUG ${XIAOYV_YOLO_OUTPUT_DIR}
            LIBRARY_OUTPUT_DIRECTORY_RELEASE ${XIAOYV_YOLO_OUTPUT_DIR}
            LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO ${XIAOYV_YOLO_OUTPUT_DIR}
            LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL ${XIAOYV_YOLO_OUTPUT_DIR})
endif()
