-- 文件用途：内置示例脚本，用于验证长循环脚本的运行和停止控制。
print("循环脚本已开始运行")

local total = 0
for i = 1, 100000000 do
    total = total + i
    if i % 10000000 == 0 then
        print("循环进度 =", i)
    end
end

print("循环脚本执行完成", total)
