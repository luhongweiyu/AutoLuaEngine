-- 文件用途：内置示例脚本，用于验证 getScreenPixels 返回宽、高、点阵地址。
print("截图脚本已开始运行")

local width, height, pixels = m.getScreenPixels()
if width then
    print("截图成功")
    print("宽度 =", width)
    print("高度 =", height)
    print("点阵地址 =", string.format("0x%x", pixels))
    print("字节长度 =", width * height * 4)
else
    print("截图失败 =", height)
end
