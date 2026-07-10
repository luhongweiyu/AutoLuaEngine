-- 文件用途：内置示例脚本，用于验证 screen_capture C ABI 返回宽、高、点阵地址。
print("screen script started")

local width, height, pixels = m.capture()
if width then
    print("screen capture success")
    print("width =", width)
    print("height =", height)
    print("pixels =", string.format("0x%x", pixels))
    print("byteLength =", width * height * 4)
else
    print("screen capture failed =", height)
end
