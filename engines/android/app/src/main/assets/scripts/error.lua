-- 文件用途：内置示例脚本，用于验证脚本错误能正确返回到 App 和 IDE。
print("about to trigger an expected error")

local text, err = m.file.read("/path/not/exist.txt")
if text ~= nil then
    error("unexpected file.read success")
end

print("expected file.read error =", err)

error("expected lua runtime error")
