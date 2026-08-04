-- 文件用途：无需模型验证 m.yolo 命名空间、兼容隔离和可选 SO 按需加载。
assert(type(m.yolo) == "table", "m.yolo 未注册")
assert(yolo == m.yolo, "默认全局 yolo 未指向 m.yolo")
assert(lr.yolo == nil, "m.yolo 不应自动复制到 lr")
assert(cd.yolo == nil, "m.yolo 不应自动复制到 cd")

local info, infoError = m.yolo.runtimeInfo()
assert(info, infoError)
assert(type(info.available) == "boolean", "runtimeInfo.available 类型错误")
assert(type(info.loaded) == "boolean", "runtimeInfo.loaded 类型错误")
print("YOLO 已导入：", info.available, "已加载：", info.loaded)

local available, availableError = m.yolo.isAvailable()
assert(available ~= nil, availableError)
assert(available == info.available, "isAvailable 与 runtimeInfo 不一致")

local loaded, loadedError = m.yolo.isLoaded("__smoke_missing__")
assert(loaded ~= nil, loadedError)
assert(loaded == false, "不存在的模型不应显示为已加载")

local released, releaseError = m.yolo.release("__smoke_missing__")
assert(released ~= nil, releaseError)
assert(released == false, "释放不存在模型应返回 false")

if available then
    local ok, loadError = m.yolo.init(
        "__missing_yolo_labels__.txt",
        "__missing_yolo_model__.param",
        "__missing_yolo_model__.bin"
    )
    assert(ok == nil, "不存在的模型文件不应加载成功")
    assert(type(loadError) == "string" and loadError ~= "", "模型加载失败应返回错误")

    local loadedInfo, loadedInfoError = m.yolo.runtimeInfo()
    assert(loadedInfo, loadedInfoError)
    assert(loadedInfo.loaded == true, "模型调用后可选 YOLO SO 应已按需加载")
    print("YOLO SO 按需加载成功：", loadedInfo.backend, loadedInfo.ncnnVersion)
end

print("YOLO Lua 运行时 smoke test 通过")
