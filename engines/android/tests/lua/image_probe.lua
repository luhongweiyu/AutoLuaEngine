-- 文件用途：验证当前缓存帧保存为模板后，findPic 能在同一帧中准确返回左上角坐标。
assert(m.setImageCacheMaxBytes(5 * 1024 * 1024) == 5 * 1024 * 1024)
m.keepCapture()

local path = "/sdcard/xiaoyv/scripts/image_probe.png"
local saved, saveError = m.capture(path)
print("保存找图模板", saved, saveError)

-- snapShot 是 capture 的完整别名；区域采用左闭右开坐标，输出尺寸应为 360x640。
local regionPath = "/sdcard/xiaoyv/scripts/image_region_probe.png"
local regionSaved, regionError = m.snapShot(regionPath, 0, 0, 360, 640)
print("保存区域截图", regionSaved, regionError)

if saved then
    -- HTTP 调试脚本没有真实脚本工作目录，直接传刚保存的绝对路径，避免相对路径测试到
    -- 调试进程的空工作目录；从 App 文件运行时也可传 "image_probe.png" 这类相对路径。
    local x, y, findError = m.findPic(0, 0, 719, 1279, path, "000000", 1, 1.0)
    print("完整模板找图", x, y, findError)
end

m.releaseCapture()
