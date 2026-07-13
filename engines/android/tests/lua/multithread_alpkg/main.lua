-- 文件用途：验证 ALPKG 主脚本创建的 native 子线程可以共享 _G 并读取同一个包内资源。
local result
local finished = false

m.thread.beginThread(function()
    result = m.read_alpkg_file("resource.txt")
    finished = true
end)

while not finished do
    sleep(5)
end

assert(result and result:match("^ALPKG_THREAD_RESOURCE%s*$"), "子线程读取 ALPKG 资源失败")
print("ALPKG 子线程资源读取通过")
