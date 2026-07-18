# 文件用途：以源码边界断言防止主窗口、设备传输和字库面板重新堆回同一个大类。

if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "缺少 SOURCE_ROOT")
endif()

function(assert_not_contains relative_path pattern description)
    file(READ "${SOURCE_ROOT}/${relative_path}" source_text)
    string(REGEX MATCH "${pattern}" violation "${source_text}")
    if(violation)
        message(FATAL_ERROR "${description}: ${relative_path} 命中 ${violation}")
    endif()
endfunction()

# 应用外壳不得重新接管图片文件、标签或待用框选。
assert_not_contains("src/main_window.cpp" "QFileDialog|QImageReader|QMimeData|pendingSelection_" "主窗口重新包含图片工作区逻辑")
assert_not_contains("src/main_window.h" "documents_|pendingSelection_|saveDocument|closeDocument" "主窗口重新保存图片工作区状态")

# 设备业务层不得直接接触网络和 ADB；传输层不得持有截图或脚本会话状态。
assert_not_contains("src/device/device_client.h" "QNetworkAccessManager|QProcess|adbForward|ensureEndpoint" "设备业务层重新包含传输实现")
assert_not_contains("src/device/device_client.cpp" "QNetworkReply|adbForward|ensureEndpoint|requestFor" "设备业务层重新包含传输实现")
assert_not_contains("src/device/device_transport.h" "scriptRunning_|busyOperations_|screenshotReceived|projectionOpened" "设备传输层重新包含业务状态")
assert_not_contains("src/device/device_transport.cpp" "scriptRunning_|busyOperations_|screenshotReceived|projectionOpened" "设备传输层重新包含业务状态")

# 点阵面板不得重新保存字库文档或文件交互状态。
assert_not_contains("src/panels/font_panel.h" "FontDictionaryDocument dictionary_|dictionaryList_|dictionaryPathEdit_|openDictionary|saveDictionary" "点阵面板重新包含字库编辑器状态")
assert_not_contains("src/panels/font_panel.cpp" "QFileDialog|QMessageBox|askDuplicateDecision|dictionary_\\." "点阵面板重新包含字库文件或冲突流程")

# 无图片时的待用范围只能存在于 ImageWorkspace。
file(GLOB_RECURSE production_sources
        "${SOURCE_ROOT}/src/*.h"
        "${SOURCE_ROOT}/src/*.cpp")
foreach(source_file IN LISTS production_sources)
    if(source_file MATCHES "workspace[/\\\\]image_workspace\\.(h|cpp)$")
        continue()
    endif()
    file(READ "${source_file}" source_text)
    if(source_text MATCHES "pendingSelection_")
        message(FATAL_ERROR "待用框选状态出现在工作区之外: ${source_file}")
    endif()
endforeach()

message(STATUS "抓图取色器架构边界检查通过")
